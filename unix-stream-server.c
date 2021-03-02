#include "cache.h"
#include "lockfile.h"
#include "unix-socket.h"
#include "unix-stream-server.h"

#define DEFAULT_UNIX_STREAM_LISTEN_TIMEOUT (100)

static int is_another_server_alive(const char *path,
				   const struct unix_stream_listen_opts *opts)
{
	struct stat st;
	int fd;

	if (!lstat(path, &st) && S_ISSOCK(st.st_mode)) {
		/*
		 * A socket-inode exists on disk at `path`, but we
		 * don't know whether it belongs to an active server
		 * or whether the last server died without cleaning
		 * up.
		 *
		 * Poke it with a trivial connection to try to find
		 * out.
		 */
		fd = unix_stream_connect(path, opts->disallow_chdir);
		if (fd >= 0) {
			close(fd);
			return 1;
		}
	}

	return 0;
}

struct unix_stream_server_socket *unix_stream_server__listen_with_lock(
	const char *path,
	const struct unix_stream_listen_opts *opts)
{
	struct lock_file lock = LOCK_INIT;
	long timeout;
	int fd_socket;
	struct unix_stream_server_socket *server_socket;

	timeout = opts->timeout_ms;
	if (opts->timeout_ms <= 0)
		timeout = DEFAULT_UNIX_STREAM_LISTEN_TIMEOUT;

	/*
	 * Create a lock at "<path>.lock" if we can.
	 */
	if (hold_lock_file_for_update_timeout(&lock, path, 0, timeout) < 0) {
		error_errno(_("could not lock listener socket '%s'"), path);
		return NULL;
	}

	/*
	 * If another server is listening on "<path>" give up.  We do not
	 * want to create a socket and steal future connections from them.
	 */
	if (is_another_server_alive(path, opts)) {
		errno = EADDRINUSE;
		error_errno(_("listener socket already in use '%s'"), path);
		rollback_lock_file(&lock);
		return NULL;
	}

	/*
	 * Create and bind to a Unix domain socket at "<path>".
	 */
	fd_socket = unix_stream_listen(path, opts);
	if (fd_socket < 0) {
		error_errno(_("could not create listener socket '%s'"), path);
		rollback_lock_file(&lock);
		return NULL;
	}

	server_socket = xcalloc(1, sizeof(*server_socket));
	server_socket->path_socket = strdup(path);
	server_socket->fd_socket = fd_socket;
	lstat(path, &server_socket->st_socket);

	/*
	 * Always rollback (just delete) "<path>.lock" because we already created
	 * "<path>" as a socket and do not want to commit_lock to do the atomic
	 * rename trick.
	 */
	rollback_lock_file(&lock);

	return server_socket;
}

void unix_stream_server__free(
	struct unix_stream_server_socket *server_socket)
{
	if (!server_socket)
		return;

	if (server_socket->fd_socket >= 0) {
		if (!unix_stream_server__was_stolen(server_socket))
			unlink(server_socket->path_socket);
		close(server_socket->fd_socket);
	}

	free(server_socket->path_socket);
	free(server_socket);
}

int unix_stream_server__was_stolen(
	struct unix_stream_server_socket *server_socket)
{
	struct stat st_now;

	if (!server_socket)
		return 0;

	if (lstat(server_socket->path_socket, &st_now) == -1)
		return 1;

	if (st_now.st_ino != server_socket->st_socket.st_ino)
		return 1;

	/* We might also consider the ctime on some platforms. */

	return 0;
}
