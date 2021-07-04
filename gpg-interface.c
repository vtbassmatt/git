#include "cache.h"
#include "commit.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "gpg-interface.h"
#include "sigchain.h"
#include "tempfile.h"

static char *configured_signing_key;
const char *ssh_allowed_signers, *ssh_revocation_file;
static enum signature_trust_level configured_min_trust_level = TRUST_UNDEFINED;

struct gpg_format {
	const char *name;
	const char *program;
	const char **verify_args;
	const char **sigs;
};

static const char *openpgp_verify_args[] = {
	"--keyid-format=long",
	NULL
};
static const char *openpgp_sigs[] = {
	"-----BEGIN PGP SIGNATURE-----",
	"-----BEGIN PGP MESSAGE-----",
	NULL
};

static const char *x509_verify_args[] = {
	NULL
};
static const char *x509_sigs[] = {
	"-----BEGIN SIGNED MESSAGE-----",
	NULL
};

static const char *ssh_verify_args[] = {
	NULL
};
static const char *ssh_sigs[] = {
	"-----BEGIN SSH SIGNATURE-----",
	NULL
};

static struct gpg_format gpg_format[] = {
	{ .name = "openpgp", .program = "gpg",
	  .verify_args = openpgp_verify_args,
	  .sigs = openpgp_sigs
	},
	{ .name = "x509", .program = "gpgsm",
	  .verify_args = x509_verify_args,
	  .sigs = x509_sigs
	},
	{ .name = "ssh", .program = "ssh-keygen",
	  .verify_args = ssh_verify_args,
	  .sigs = ssh_sigs },
};

static struct gpg_format *use_format = &gpg_format[0];

static struct gpg_format *get_format_by_name(const char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gpg_format); i++)
		if (!strcmp(gpg_format[i].name, str))
			return gpg_format + i;
	return NULL;
}

static struct gpg_format *get_format_by_sig(const char *sig)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(gpg_format); i++)
		for (j = 0; gpg_format[i].sigs[j]; j++)
			if (starts_with(sig, gpg_format[i].sigs[j]))
				return gpg_format + i;
	return NULL;
}

void signature_check_clear(struct signature_check *sigc)
{
	FREE_AND_NULL(sigc->payload);
	FREE_AND_NULL(sigc->gpg_output);
	FREE_AND_NULL(sigc->gpg_status);
	FREE_AND_NULL(sigc->signer);
	FREE_AND_NULL(sigc->key);
	FREE_AND_NULL(sigc->fingerprint);
	FREE_AND_NULL(sigc->primary_key_fingerprint);
}

/* An exclusive status -- only one of them can appear in output */
#define GPG_STATUS_EXCLUSIVE	(1<<0)
/* The status includes key identifier */
#define GPG_STATUS_KEYID	(1<<1)
/* The status includes user identifier */
#define GPG_STATUS_UID		(1<<2)
/* The status includes key fingerprints */
#define GPG_STATUS_FINGERPRINT	(1<<3)
/* The status includes trust level */
#define GPG_STATUS_TRUST_LEVEL	(1<<4)

/* Short-hand for standard exclusive *SIG status with keyid & UID */
#define GPG_STATUS_STDSIG	(GPG_STATUS_EXCLUSIVE|GPG_STATUS_KEYID|GPG_STATUS_UID)

static struct {
	char result;
	const char *check;
	unsigned int flags;
} sigcheck_gpg_status[] = {
	{ 'G', "GOODSIG ", GPG_STATUS_STDSIG },
	{ 'B', "BADSIG ", GPG_STATUS_STDSIG },
	{ 'E', "ERRSIG ", GPG_STATUS_EXCLUSIVE|GPG_STATUS_KEYID },
	{ 'X', "EXPSIG ", GPG_STATUS_STDSIG },
	{ 'Y', "EXPKEYSIG ", GPG_STATUS_STDSIG },
	{ 'R', "REVKEYSIG ", GPG_STATUS_STDSIG },
	{ 0, "VALIDSIG ", GPG_STATUS_FINGERPRINT },
	{ 0, "TRUST_", GPG_STATUS_TRUST_LEVEL },
};

static struct {
	const char *key;
	enum signature_trust_level value;
} sigcheck_gpg_trust_level[] = {
	{ "UNDEFINED", TRUST_UNDEFINED },
	{ "NEVER", TRUST_NEVER },
	{ "MARGINAL", TRUST_MARGINAL },
	{ "FULLY", TRUST_FULLY },
	{ "ULTIMATE", TRUST_ULTIMATE },
};

static void replace_cstring(char **field, const char *line, const char *next)
{
	free(*field);

	if (line && next)
		*field = xmemdupz(line, next - line);
	else
		*field = NULL;
}

static int parse_gpg_trust_level(const char *level,
				 enum signature_trust_level *res)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sigcheck_gpg_trust_level); i++) {
		if (!strcmp(sigcheck_gpg_trust_level[i].key, level)) {
			*res = sigcheck_gpg_trust_level[i].value;
			return 0;
		}
	}
	return 1;
}

static void parse_ssh_output(struct signature_check *sigc)
{
	const char *output = NULL;
	char *next = NULL;

	// ssh-keysign output should be:
	// Good "git" signature for PRINCIPAL with RSA key SHA256:FINGERPRINT

	output = xmemdupz(sigc->gpg_status, strcspn(sigc->gpg_status, " \n"));
	if (skip_prefix(sigc->gpg_status, "Good \"git\" signature for ", &output)) {
		sigc->result = 'G';

		next = strchrnul(output, ' ');
		replace_cstring(&sigc->signer, output, next);
		output = next + 1;
		next = strchrnul(output, ' '); // 'with'
		output = next + 1;
		next = strchrnul(output, ' '); // KEY Type
		output = next + 1;
		next = strchrnul(output, ' '); // 'key'
		output = next + 1;
		next = strchrnul(output, ' '); // key
		replace_cstring(&sigc->fingerprint, output, next);
	} else {
		sigc->result = 'B';
	}

	// SSH-Keygen prints onto stdout instead of stderr like the output code expects - so we just copy it over
	free(sigc->gpg_output);
	sigc->gpg_output = xmemdupz(sigc->gpg_status, strlen(sigc->gpg_status));
}

static void parse_gpg_output(struct signature_check *sigc)
{
	const char *buf = sigc->gpg_status;
	const char *line, *next;
	int i, j;
	int seen_exclusive_status = 0;

	/* Iterate over all lines */
	for (line = buf; *line; line = strchrnul(line+1, '\n')) {
		while (*line == '\n')
			line++;
		if (!*line)
			break;

		/* Skip lines that don't start with GNUPG status */
		if (!skip_prefix(line, "[GNUPG:] ", &line))
			continue;

		/* Iterate over all search strings */
		for (i = 0; i < ARRAY_SIZE(sigcheck_gpg_status); i++) {
			if (skip_prefix(line, sigcheck_gpg_status[i].check, &line)) {
				/*
				 * GOODSIG, BADSIG etc. can occur only once for
				 * each signature.  Therefore, if we had more
				 * than one then we're dealing with multiple
				 * signatures.  We don't support them
				 * currently, and they're rather hard to
				 * create, so something is likely fishy and we
				 * should reject them altogether.
				 */
				if (sigcheck_gpg_status[i].flags & GPG_STATUS_EXCLUSIVE) {
					if (seen_exclusive_status++)
						goto error;
				}

				if (sigcheck_gpg_status[i].result)
					sigc->result = sigcheck_gpg_status[i].result;
				/* Do we have key information? */
				if (sigcheck_gpg_status[i].flags & GPG_STATUS_KEYID) {
					next = strchrnul(line, ' ');
					replace_cstring(&sigc->key, line, next);
					/* Do we have signer information? */
					if (*next && (sigcheck_gpg_status[i].flags & GPG_STATUS_UID)) {
						line = next + 1;
						next = strchrnul(line, '\n');
						replace_cstring(&sigc->signer, line, next);
					}
				}

				/* Do we have trust level? */
				if (sigcheck_gpg_status[i].flags & GPG_STATUS_TRUST_LEVEL) {
					/*
					 * GPG v1 and v2 differs in how the
					 * TRUST_ lines are written.  Some
					 * trust lines contain no additional
					 * space-separated information for v1.
					 */
					size_t trust_size = strcspn(line, " \n");
					char *trust = xmemdupz(line, trust_size);

					if (parse_gpg_trust_level(trust, &sigc->trust_level)) {
						free(trust);
						goto error;
					}
					free(trust);
				}

				/* Do we have fingerprint? */
				if (sigcheck_gpg_status[i].flags & GPG_STATUS_FINGERPRINT) {
					const char *limit;
					char **field;

					next = strchrnul(line, ' ');
					replace_cstring(&sigc->fingerprint, line, next);

					/*
					 * Skip interim fields.  The search is
					 * limited to the same line since only
					 * OpenPGP signatures has a field with
					 * the primary fingerprint.
					 */
					limit = strchrnul(line, '\n');
					for (j = 9; j > 0; j--) {
						if (!*next || limit <= next)
							break;
						line = next + 1;
						next = strchrnul(line, ' ');
					}

					field = &sigc->primary_key_fingerprint;
					if (!j) {
						next = strchrnul(line, '\n');
						replace_cstring(field, line, next);
					} else {
						replace_cstring(field, NULL, NULL);
					}
				}

				break;
			}
		}
	}
	return;

error:
	sigc->result = 'E';
	/* Clear partial data to avoid confusion */
	FREE_AND_NULL(sigc->primary_key_fingerprint);
	FREE_AND_NULL(sigc->fingerprint);
	FREE_AND_NULL(sigc->signer);
	FREE_AND_NULL(sigc->key);
}

static int verify_signed_buffer(const char *payload, size_t payload_size,
				const char *signature, size_t signature_size,
				struct strbuf *gpg_output,
				struct strbuf *gpg_status)
{
	struct child_process gpg = CHILD_PROCESS_INIT,
			     ssh_keygen = CHILD_PROCESS_INIT;
	struct gpg_format *fmt;
	struct tempfile *temp;
	int ret;
	const char *line;
	size_t trust_size;
	char *principal;
	struct strbuf buf = STRBUF_INIT,
		      principal_out = STRBUF_INIT,
		      principal_err = STRBUF_INIT;

	temp = mks_tempfile_t(".git_vtag_tmpXXXXXX");
	if (!temp)
		return error_errno(_("could not create temporary file"));
	if (write_in_full(temp->fd, signature, signature_size) < 0 ||
	    close_tempfile_gently(temp) < 0) {
		error_errno(_("failed writing detached signature to '%s'"),
			    temp->filename.buf);
		delete_tempfile(&temp);
		return -1;
	}

	fmt = get_format_by_sig(signature);
	if (!fmt)
		BUG("bad signature '%s'", signature);

	if (!strcmp(use_format->name, "ssh")) {
		// Find the principal from the  signers
		strvec_push(&ssh_keygen.args, fmt->program);
		strvec_pushl(&ssh_keygen.args,  "-Y", "find-principals",
						"-f", get_ssh_allowed_signers(),
						"-s", temp->filename.buf,
						NULL);
		ret = pipe_command(&ssh_keygen, NULL, 0, &principal_out, 0, &principal_err, 0);
		if (strstr(principal_err.buf, "unknown option")) {
			error(_("openssh version > 8.2p1 is needed for ssh signature verification (ssh-keygen needs -Y find-principals/verify option)"));
		}
		if (ret || !principal_out.len)
			goto out;

		/* Iterate over all lines */
		for (line = principal_out.buf; *line; line = strchrnul(line + 1, '\n')) {
			while (*line == '\n')
				line++;
			if (!*line)
				break;

			trust_size = strcspn(line, " \n");
			principal = xmemdupz(line, trust_size);

			strvec_push(&gpg.args,fmt->program);
			// We found principals - Try with each until we find a match
			strvec_pushl(&gpg.args, "-Y", "verify",
						"-n", "git",
						"-f", get_ssh_allowed_signers(),
						"-I", principal,
						"-s", temp->filename.buf,
						 NULL);

			if (ssh_revocation_file) {
				strvec_pushl(&gpg.args, "-r", ssh_revocation_file, NULL);
			}

			if (!gpg_status)
				gpg_status = &buf;

			sigchain_push(SIGPIPE, SIG_IGN);
			ret = pipe_command(&gpg, payload, payload_size,
					   gpg_status, 0, gpg_output, 0);
			sigchain_pop(SIGPIPE);

			ret |= !strstr(gpg_status->buf, "Good");
			if (ret == 0)
				break;
		}
	} else {
		strvec_push(&gpg.args, fmt->program);
		strvec_pushv(&gpg.args, fmt->verify_args);
		strvec_pushl(&gpg.args,
				"--status-fd=1",
				"--verify", temp->filename.buf, "-",
				NULL);

		if (!gpg_status)
			gpg_status = &buf;

		sigchain_push(SIGPIPE, SIG_IGN);
		ret = pipe_command(&gpg, payload, payload_size, gpg_status, 0,
				   gpg_output, 0);
		sigchain_pop(SIGPIPE);
		ret |= !strstr(gpg_status->buf, "\n[GNUPG:] GOODSIG ");
	}

out:
	delete_tempfile(&temp);
	strbuf_release(&principal_out);
	strbuf_release(&principal_err);
	strbuf_release(&buf); /* no matter it was used or not */

	return ret;
}

int check_signature(const char *payload, size_t plen, const char *signature,
	size_t slen, struct signature_check *sigc)
{
	struct strbuf gpg_output = STRBUF_INIT;
	struct strbuf gpg_status = STRBUF_INIT;
	int status;

	sigc->result = 'N';
	sigc->trust_level = -1;

	status = verify_signed_buffer(payload, plen, signature, slen,
				      &gpg_output, &gpg_status);
	if (status && !gpg_output.len)
		goto out;
	sigc->payload = xmemdupz(payload, plen);
	sigc->gpg_output = strbuf_detach(&gpg_output, NULL);
	sigc->gpg_status = strbuf_detach(&gpg_status, NULL);
	if (!strcmp(use_format->name, "ssh")) {
		parse_ssh_output(sigc);
	} else {
		parse_gpg_output(sigc);
	}
	status |= sigc->result != 'G';
	status |= sigc->trust_level < configured_min_trust_level;

 out:
	strbuf_release(&gpg_status);
	strbuf_release(&gpg_output);

	return !!status;
}

void print_signature_buffer(const struct signature_check *sigc, unsigned flags)
{
	const char *output = flags & GPG_VERIFY_RAW ?
		sigc->gpg_status : sigc->gpg_output;

	if (flags & GPG_VERIFY_VERBOSE && sigc->payload)
		fputs(sigc->payload, stdout);

	if (output)
		fputs(output, stderr);
}

size_t parse_signed_buffer(const char *buf, size_t size)
{
	size_t len = 0;
	size_t match = size;
	while (len < size) {
		const char *eol;

		if (get_format_by_sig(buf + len))
			match = len;

		eol = memchr(buf + len, '\n', size - len);
		len += eol ? eol - (buf + len) + 1 : size - len;
	}
	return match;
}

int parse_signature(const char *buf, size_t size, struct strbuf *payload, struct strbuf *signature)
{
	size_t match = parse_signed_buffer(buf, size);
	if (match != size) {
		strbuf_add(payload, buf, match);
		remove_signature(payload);
		strbuf_add(signature, buf + match, size - match);
		return 1;
	}
	return 0;
}

void set_signing_key(const char *key)
{
	free(configured_signing_key);
	configured_signing_key = xstrdup(key);
}

int git_gpg_config(const char *var, const char *value, void *cb)
{
	struct gpg_format *fmt = NULL;
	char *fmtname = NULL;
	char *trust;
	int ret;

	if (!strcmp(var, "user.signingkey")) {
		if (!value)
			return config_error_nonbool(var);
		set_signing_key(value);
		return 0;
	}

	if (!strcmp(var, "gpg.ssh.allowedsigners")) {
		return git_config_string(&ssh_allowed_signers, var, value);
	}

	if (!strcmp(var, "gpg.ssh.revocationfile")) {
		return git_config_string(&ssh_revocation_file, var, value);
	}

	if (!strcmp(var, "gpg.format")) {
		if (!value)
			return config_error_nonbool(var);
		fmt = get_format_by_name(value);
		if (!fmt)
			return error("unsupported value for %s: %s",
				     var, value);
		use_format = fmt;
		return 0;
	}

	if (!strcmp(var, "gpg.mintrustlevel")) {
		if (!value)
			return config_error_nonbool(var);

		trust = xstrdup_toupper(value);
		ret = parse_gpg_trust_level(trust, &configured_min_trust_level);
		free(trust);

		if (ret)
			return error("unsupported value for %s: %s", var,
				     value);
		return 0;
	}

	if (!strcmp(var, "gpg.program") || !strcmp(var, "gpg.openpgp.program"))
		fmtname = "openpgp";

	if (!strcmp(var, "gpg.x509.program"))
		fmtname = "x509";

	if (!strcmp(var, "gpg.ssh.program"))
		fmtname = "ssh";

	if (fmtname) {
		fmt = get_format_by_name(fmtname);
		return git_config_string(&fmt->program, var, value);
	}

	return 0;
}

const char *get_signing_key(void)
{
	if (configured_signing_key)
		return configured_signing_key;
	if (!strcmp(use_format->name, "ssh")) {
		// We could simply use the first key listed by ssh-add -L and risk signing with the wrong key
		return "";
	} else {
		return git_committer_info(IDENT_STRICT | IDENT_NO_DATE);
	}
}

const char *get_ssh_allowed_signers(void)
{
	if (ssh_allowed_signers)
		return ssh_allowed_signers;
	return GPG_SSH_ALLOWED_SIGNERS;
}

int sign_buffer(struct strbuf *buffer, struct strbuf *signature, const char *signing_key)
{
	struct child_process gpg = CHILD_PROCESS_INIT;
	int ret;
	size_t i, j, bottom;
	struct strbuf gpg_status = STRBUF_INIT;
	struct tempfile *temp = NULL;

	if (!strcmp(use_format->name, "ssh")) {
		if (!signing_key)
			return error(_("user.signingkey needs to be set to a ssh public key for ssh signing"));

		// signing_key is a public ssh key
		// FIXME: Allow specifying a key file so we can use private keyfiles instead of ssh-agent
		temp = mks_tempfile_t(".git_signing_key_tmpXXXXXX");
		if (!temp)
			return error_errno(_("could not create temporary file"));
		if (write_in_full(temp->fd, signing_key,
					strlen(signing_key)) < 0 ||
			close_tempfile_gently(temp) < 0) {
			error_errno(_("failed writing ssh signing key to '%s'"), temp->filename.buf);
			delete_tempfile(&temp);
			return -1;
		}
		strvec_pushl(&gpg.args, use_format->program ,
					"-Y", "sign",
					"-n", "git",
					"-f", temp->filename.buf,
					NULL);
	} else {
		strvec_pushl(&gpg.args, use_format->program ,
					"--status-fd=2",
					"-bsau", signing_key,
					NULL);
	}

	bottom = signature->len;

	/*
	 * When the username signingkey is bad, program could be terminated
	 * because gpg exits without reading and then write gets SIGPIPE.
	 */
	sigchain_push(SIGPIPE, SIG_IGN);
	ret = pipe_command(&gpg, buffer->buf, buffer->len,
			   signature, 1024, &gpg_status, 0);
	sigchain_pop(SIGPIPE);

	if (temp)
		delete_tempfile(&temp);

	if (!strcmp(use_format->name, "ssh")) {
		if (strstr(gpg_status.buf, "unknown option")) {
			error(_("openssh version > 8.2p1 is needed for ssh signing (ssh-keygen needs -Y sign option)"));
		}
	} else {
		ret |= !strstr(gpg_status.buf, "\n[GNUPG:] SIG_CREATED ");
	}
	strbuf_release(&gpg_status);
	if (ret)
		return error(_("gpg failed to sign the data"));

	/* Strip CR from the line endings, in case we are on Windows. */
	for (i = j = bottom; i < signature->len; i++)
		if (signature->buf[i] != '\r') {
			if (i != j)
				signature->buf[j] = signature->buf[i];
			j++;
		}
	strbuf_setlen(signature, j);

	return 0;
}
