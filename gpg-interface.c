#include "cache.h"
#include "commit.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "dir.h"
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
	FREE_AND_NULL(sigc->output);
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
	struct string_list parts = STRING_LIST_INIT_DUP;
	char *line = NULL;

	/*
	 * ssh-keysign output should be:
	 * Good "git" signature for PRINCIPAL with RSA key SHA256:FINGERPRINT
	 * or for valid but unknown keys:
	 * Good "git" signature with RSA key SHA256:FINGERPRINT
	 */
	sigc->result = 'B';
	sigc->trust_level = TRUST_NEVER;

	line = xmemdupz(sigc->output, strcspn(sigc->output, "\n"));
	string_list_split(&parts, line, ' ', 8);
	if (parts.nr >= 9 && starts_with(line, "Good \"git\" signature for ")) {
		/* Valid signature for a trusted signer */
		sigc->result = 'G';
		sigc->trust_level = TRUST_FULLY;
		sigc->signer = xstrdup(parts.items[4].string);
		sigc->fingerprint = xstrdup(parts.items[8].string);
		sigc->key = xstrdup(sigc->fingerprint);
	} else if (parts.nr >= 7 && starts_with(line, "Good \"git\" signature with ")) {
		/* Valid signature, but key unknown */
		sigc->result = 'G';
		sigc->trust_level = TRUST_UNDEFINED;
		sigc->fingerprint = xstrdup(parts.items[6].string);
		sigc->key = xstrdup(sigc->fingerprint);
	}
	trace_printf("trace: sigc result %c/%d - %s %s %s", sigc->result, sigc->trust_level, sigc->signer, sigc->fingerprint, sigc->key);

	string_list_clear(&parts, 0);
	FREE_AND_NULL(line);
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

static int verify_ssh_signature(struct signature_check *sigc,
	struct gpg_format *fmt,
	const char *payload, size_t payload_size,
	const char *signature, size_t signature_size)
{
	struct child_process ssh_keygen = CHILD_PROCESS_INIT;
	struct tempfile *temp;
	int ret;
	const char *line;
	size_t trust_size;
	char *principal;
	struct strbuf ssh_keygen_out = STRBUF_INIT;
	struct strbuf ssh_keygen_err = STRBUF_INIT;

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

	/* Find the principal from the signers */
	strvec_pushl(&ssh_keygen.args,  fmt->program,
					"-Y", "find-principals",
					"-f", get_ssh_allowed_signers(),
					"-s", temp->filename.buf,
					NULL);
	ret = pipe_command(&ssh_keygen, NULL, 0, &ssh_keygen_out, 0, &ssh_keygen_err, 0);
	if (strstr(ssh_keygen_err.buf, "usage:")) {
		error(_("openssh version > 8.2p1 is needed for ssh signature verification (ssh-keygen needs -Y find-principals/verify option)"));
	}
	if (ret || !ssh_keygen_out.len) {
		/* We did not find a matching principal in the keyring - Check without validation */
		child_process_init(&ssh_keygen);
		strvec_pushl(&ssh_keygen.args,  fmt->program,
						"-Y", "check-novalidate",
						"-n", "git",
						"-s", temp->filename.buf,
						NULL);
		ret = pipe_command(&ssh_keygen, payload, payload_size, &ssh_keygen_out, 0, &ssh_keygen_err, 0);
	} else {
		/* Check every principal we found (one per line) */
		for (line = ssh_keygen_out.buf; *line; line = strchrnul(line + 1, '\n')) {
			while (*line == '\n')
				line++;
			if (!*line)
				break;

			trust_size = strcspn(line, " \n");
			principal = xmemdupz(line, trust_size);

			child_process_init(&ssh_keygen);
			strbuf_release(&ssh_keygen_out);
			strbuf_release(&ssh_keygen_err);
			strvec_push(&ssh_keygen.args,fmt->program);
			/* We found principals - Try with each until we find a match */
			strvec_pushl(&ssh_keygen.args,  "-Y", "verify",
							"-n", "git",
							"-f", get_ssh_allowed_signers(),
							"-I", principal,
							"-s", temp->filename.buf,
							NULL);

			if (ssh_revocation_file) {
				if (file_exists(ssh_revocation_file)) {
					strvec_pushl(&ssh_keygen.args, "-r", ssh_revocation_file, NULL);
				} else {
					warning(_("ssh signing revocation file configured but not found: %s"), ssh_revocation_file);
				}
			}

			sigchain_push(SIGPIPE, SIG_IGN);
			ret = pipe_command(&ssh_keygen, payload, payload_size,
					&ssh_keygen_out, 0, &ssh_keygen_err, 0);
			sigchain_pop(SIGPIPE);

			ret &= starts_with(ssh_keygen_out.buf, "Good");
			if (ret == 0)
				break;
		}
	}

	sigc->payload = xmemdupz(payload, payload_size);
	strbuf_stripspace(&ssh_keygen_out, 0);
	strbuf_stripspace(&ssh_keygen_err, 0);
	strbuf_add(&ssh_keygen_out, ssh_keygen_err.buf, ssh_keygen_err.len);
	sigc->output = strbuf_detach(&ssh_keygen_out, NULL);
	sigc->gpg_status = xstrdup(sigc->output);

	parse_ssh_output(sigc);

	delete_tempfile(&temp);
	strbuf_release(&ssh_keygen_out);
	strbuf_release(&ssh_keygen_err);

	return ret;
}

static int verify_gpg_signature(struct signature_check *sigc, struct gpg_format *fmt,
	const char *payload, size_t payload_size,
	const char *signature, size_t signature_size)
{
	struct child_process gpg = CHILD_PROCESS_INIT;
	struct tempfile *temp;
	int ret;
	struct strbuf gpg_out = STRBUF_INIT;
	struct strbuf gpg_err = STRBUF_INIT;

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

	strvec_push(&gpg.args, fmt->program);
	strvec_pushv(&gpg.args, fmt->verify_args);
	strvec_pushl(&gpg.args,
			"--status-fd=1",
			"--verify", temp->filename.buf, "-",
			NULL);

	sigchain_push(SIGPIPE, SIG_IGN);
	ret = pipe_command(&gpg, payload, payload_size, &gpg_out, 0,
				&gpg_err, 0);
	sigchain_pop(SIGPIPE);
	ret |= !strstr(gpg_out.buf, "\n[GNUPG:] GOODSIG ");

	sigc->payload = xmemdupz(payload, payload_size);
	sigc->output = strbuf_detach(&gpg_err, NULL);
	sigc->gpg_status = strbuf_detach(&gpg_out, NULL);

	parse_gpg_output(sigc);

	delete_tempfile(&temp);
	strbuf_release(&gpg_out);
	strbuf_release(&gpg_err);

	return ret;
}

int check_signature(const char *payload, size_t plen, const char *signature,
	size_t slen, struct signature_check *sigc)
{
	struct gpg_format *fmt;
	int status;

	sigc->result = 'N';
	sigc->trust_level = -1;

	fmt = get_format_by_sig(signature);
	if (!fmt) {
		error(_("bad/incompatible signature '%s'"), signature);
		return -1;
	}

	if (!strcmp(fmt->name, "ssh")) {
		status = verify_ssh_signature(sigc, fmt, payload, plen, signature, slen);
	} else {
		status = verify_gpg_signature(sigc, fmt, payload, plen, signature, slen);
	}
	if (status && !sigc->output)
		return !!status;

	status |= sigc->result != 'G';
	status |= sigc->trust_level < configured_min_trust_level;

	return !!status;
}

void print_signature_buffer(const struct signature_check *sigc, unsigned flags)
{
	const char *output = flags & GPG_VERIFY_RAW ?
		sigc->gpg_status : sigc->output;

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
		/*
		 * user.signingkey can contain one of the following
		 * when format = openpgp/x509
		 *   - GPG KeyID
		 * when format = ssh
		 *   - literal ssh public key (e.g. ssh-rsa XXXKEYXXX comment)
		 *   - path to a file containing a public or a private ssh key
		 */
		if (!value)
			return config_error_nonbool(var);
		set_signing_key(value);
		return 0;
	}

	if (!strcmp(var, "gpg.ssh.keyring")) {
		if (!value)
			return config_error_nonbool(var);
		return git_config_string(&ssh_allowed_signers, var, value);
	}

	if (!strcmp(var, "gpg.ssh.revocationkeyring")) {
		if (!value)
			return config_error_nonbool(var);
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

static char *get_ssh_key_fingerprint(const char *signing_key) {
	struct child_process ssh_keygen = CHILD_PROCESS_INIT;
	int ret = -1;
	struct strbuf fingerprint_stdout = STRBUF_INIT;
	struct strbuf **fingerprint;

	/*
	 * With SSH Signing this can contain a filename or a public key
	 * For textual representation we usually want a fingerprint
	 */
	if (istarts_with(signing_key, "ssh-")) {
		strvec_pushl(&ssh_keygen.args, "ssh-keygen",
					"-lf", "-",
					NULL);
		ret = pipe_command(&ssh_keygen, signing_key, strlen(signing_key),
			&fingerprint_stdout, 0,  NULL, 0);
	} else {
		strvec_pushl(&ssh_keygen.args, "ssh-keygen",
					"-lf", configured_signing_key,
					NULL);
		ret = pipe_command(&ssh_keygen, NULL, 0, &fingerprint_stdout, 0,
			NULL, 0);
	}

	if (!!ret)
		die_errno(_("failed to get the ssh fingerprint for key '%s'"),
			signing_key);

	fingerprint = strbuf_split_max(&fingerprint_stdout, ' ', 3);
	if (!fingerprint[1])
		die_errno(_("failed to get the ssh fingerprint for key '%s'"),
			signing_key);

	return strbuf_detach(fingerprint[1], NULL);
}

/* Returns the first public key from an ssh-agent to use for signing */
static char *get_default_ssh_signing_key(void) {
	struct child_process ssh_add = CHILD_PROCESS_INIT;
	int ret = -1;
	struct strbuf key_stdout = STRBUF_INIT;
	struct strbuf **keys;

	strvec_pushl(&ssh_add.args, "ssh-add", "-L", NULL);
	ret = pipe_command(&ssh_add, NULL, 0, &key_stdout, 0, NULL, 0);
	if (!ret) {
		keys = strbuf_split_max(&key_stdout, '\n', 2);
		if (keys[0])
			return strbuf_detach(keys[0], NULL);
	}

	return "";
}

/* Returns a textual but unique representation ot the signing key */
const char *get_signing_key_id(void) {
	if (!strcmp(use_format->name, "ssh")) {
		return get_ssh_key_fingerprint(get_signing_key());
	} else {
		/* GPG/GPGSM only store a key id on this variable */
		return get_signing_key();
	}
}

const char *get_signing_key(void)
{
	if (configured_signing_key)
		return configured_signing_key;
	if (!strcmp(use_format->name, "ssh")) {
		return get_default_ssh_signing_key();
	} else {
		return git_committer_info(IDENT_STRICT | IDENT_NO_DATE);
	}
}

const char *get_ssh_allowed_signers(void)
{
	if (ssh_allowed_signers)
		return ssh_allowed_signers;

	die("A Path to an allowed signers ssh keyring is needed for validation");
}

int sign_buffer(struct strbuf *buffer, struct strbuf *signature, const char *signing_key)
{
	struct child_process signer = CHILD_PROCESS_INIT;
	int ret;
	size_t i, j, bottom;
	struct strbuf signer_stderr = STRBUF_INIT;
	struct tempfile *temp = NULL, *buffer_file = NULL;
	char *ssh_signing_key_file = NULL;
	struct strbuf ssh_signature_filename = STRBUF_INIT;

	if (!strcmp(use_format->name, "ssh")) {
		if (!signing_key || signing_key[0] == '\0')
			return error(_("user.signingkey needs to be set for ssh signing"));


		if (istarts_with(signing_key, "ssh-")) {
			/* A literal ssh key */
			temp = mks_tempfile_t(".git_signing_key_tmpXXXXXX");
			if (!temp)
				return error_errno(_("could not create temporary file"));
			if (write_in_full(temp->fd, signing_key, strlen(signing_key)) < 0 ||
				close_tempfile_gently(temp) < 0) {
				error_errno(_("failed writing ssh signing key to '%s'"),
					temp->filename.buf);
				delete_tempfile(&temp);
				return -1;
			}
			ssh_signing_key_file= temp->filename.buf;
		} else {
			/* We assume a file */
			ssh_signing_key_file = expand_user_path(signing_key, 1);
		}

		buffer_file = mks_tempfile_t(".git_signing_buffer_tmpXXXXXX");
		if (!buffer_file)
			return error_errno(_("could not create temporary file"));
		if (write_in_full(buffer_file->fd, buffer->buf, buffer->len) < 0 ||
			close_tempfile_gently(buffer_file) < 0) {
			error_errno(_("failed writing ssh signing key buffer to '%s'"),
				buffer_file->filename.buf);
			delete_tempfile(&buffer_file);
			return -1;
		}

		strvec_pushl(&signer.args, use_format->program ,
					"-Y", "sign",
					"-n", "git",
					"-f", ssh_signing_key_file,
					buffer_file->filename.buf,
					NULL);

		sigchain_push(SIGPIPE, SIG_IGN);
		ret = pipe_command(&signer, NULL, 0, NULL, 0, &signer_stderr, 0);
		sigchain_pop(SIGPIPE);

		strbuf_addbuf(&ssh_signature_filename, &buffer_file->filename);
		strbuf_addstr(&ssh_signature_filename, ".sig");
		if (strbuf_read_file(signature, ssh_signature_filename.buf, 2048) < 0) {
			error_errno(_("failed reading ssh signing data buffer from '%s'"),
				ssh_signature_filename.buf);
		}
		unlink_or_warn(ssh_signature_filename.buf);
		strbuf_release(&ssh_signature_filename);
		delete_tempfile(&buffer_file);
	} else {
		strvec_pushl(&signer.args, use_format->program ,
		     "--status-fd=2",
		     "-bsau", signing_key,
		     NULL);

	/*
	 * When the username signingkey is bad, program could be terminated
	 * because gpg exits without reading and then write gets SIGPIPE.
	 */
	sigchain_push(SIGPIPE, SIG_IGN);
		ret = pipe_command(&signer, buffer->buf, buffer->len, signature, 1024, &signer_stderr, 0);
	sigchain_pop(SIGPIPE);
	}

	bottom = signature->len;

	if (temp)
		delete_tempfile(&temp);

	if (!strcmp(use_format->name, "ssh")) {
		if (strstr(signer_stderr.buf, "usage:")) {
			error(_("openssh version > 8.2p1 is needed for ssh signing (ssh-keygen needs -Y sign option)"));
		}
	} else {
		ret |= !strstr(signer_stderr.buf, "\n[GNUPG:] SIG_CREATED ");
	}
	strbuf_release(&signer_stderr);
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
