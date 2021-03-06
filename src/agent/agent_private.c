#include "agent_private.h"

#include <unistd.h>
#include <sys/select.h>

int agent_wait(agent *a)
{
	fd_set rfds;
	/* Wait for 1 second */
	struct timeval tv = {
		.tv_sec = 2,
		.tv_usec = 0
	};
	int ret;

	FD_ZERO(&rfds);
	FD_SET(a->in, &rfds);
	ret = select(a->in+1, &rfds, NULL, NULL, &tv);

	if (ret == -1) {
		perror("select");
		return 2;
	} else if (ret == 0) {
		return 1;
	} else {
		/* Data arrived */
		return AGENT_OK;
	}
}

/* Will either fail or complete successfully returning 0 */
static int agent_read(const int fd, void *data, size_t len) 
{
	static char buff[300];
	static char *buff_pos = buff;

	/* Number of buffered bytes */
	static ssize_t buffered = 0;
	void *data_pos = data;

	/* Each time we read data there might be some data which were
	 * read previously and are kept in static buffer. First we utilize
	 * this data and then read any new (possibly buffering anything more */
	for (;;) {
		/* Copy all buffered data */
		if (buffered) {
			const size_t to_go = (size_t)buffered < len ? (size_t)buffered : len;
			memcpy(data_pos, buff_pos, to_go);
			len -= to_go;
			buffered -= to_go;
			data_pos += to_go;
			buff_pos += to_go;			
		}

		if (len == 0) {
			return AGENT_OK;
		}

		/* Need some more; buffered is empty now */
		buff_pos = buff;
		buffered = read(fd, buff, sizeof(buff));
		if (buffered == 0) {
			return AGENT_ERR_DISCONNECT;
		}

		if (buffered < 0) {
			return AGENT_ERR_DISCONNECT;
		}
	}
}

/* Will either fail or complete successfully returning 0 */
static int agent_write(const int fd, const void *buf, const size_t len)
{
	ssize_t ret;
	ret = write(fd, buf, len);
	if (ret == -1) {
		/* Probably errno == EPIPE. That is - second
		 * end disconnected */
		return AGENT_ERR_DISCONNECT;
	}
	assert((size_t)ret == len);
	if ((size_t)ret != len)
		return 1;
	return AGENT_OK;
}

#define _send(field)	  \
	do { \
		ret = agent_write(fd, &a->shdr.field, sizeof(a->shdr.field)); \
		if (ret != AGENT_OK) \
			return ret; \
	} while (0);

#define _recv(field)	  \
	do { \
		ret = agent_read(fd, &a->rhdr.field, sizeof(a->rhdr.field)); \
		if (ret != AGENT_OK) \
			return ret; \
	} while (0);

int agent_hdr_send(const agent *a) 
{
	const int fd = a->out;
	int ret = 1;

	/* Make sure we haven't locked state when using pipes */
	if (a->s && ppp_is_locked(a->s)) {
		print(PRINT_ERROR, "It's locked!\n");
		return AGENT_ERR;
	}
	assert(!a->s || !ppp_is_locked(a->s));


	_send(protocol_version);
	_send(type);
	_send(status);
	_send(int_arg);
	_send(int_arg2);
	_send(num_arg);

	ret = agent_write(fd, a->shdr.str_arg, sizeof(a->shdr.str_arg));
	if (ret != 0) {
		return ret;
	}

	return AGENT_OK;
}

int agent_hdr_recv(agent *a) 
{
	const int fd = a->in;
	int ret = 1;

	/* Make sure we haven't locked state when using pipes */
	if (a->s && ppp_is_locked(a->s)) {
		print(PRINT_ERROR, "It's locked!\n");
		return AGENT_ERR;
	}

	assert(!a->s || !ppp_is_locked(a->s));

	_recv(protocol_version);
	_recv(type);
	_recv(status);
	_recv(int_arg);
	_recv(int_arg2);
	_recv(num_arg);

	ret = agent_read(fd, a->rhdr.str_arg, sizeof(a->rhdr.str_arg));
	if (ret != AGENT_OK) {
		return ret;
	}

	if (a->rhdr.protocol_version != AGENT_PROTOCOL_VERSION) {
		print(PRINT_ERROR, "Protocol mismatch detected (%u != %u)\n", 
		      a->rhdr.protocol_version, AGENT_PROTOCOL_VERSION);
		return AGENT_ERR_PROTOCOL_MISMATCH;
	}
	return AGENT_OK;
}

void agent_hdr_init(agent *a, int status)
{
	a->shdr.protocol_version = AGENT_PROTOCOL_VERSION;
	a->shdr.status = status;

	a->shdr.int_arg = a->shdr.int_arg2 = 0;
	a->shdr.num_arg = num_i(0);
	memset(a->shdr.str_arg, 0, sizeof(a->shdr.str_arg));
}

void agent_hdr_sanitize(agent *a)
{
	agent_hdr_init(a, 0);
}

void agent_hdr_set_num(agent *a, const num_t *num_arg)
{
	if (num_arg)
		a->shdr.num_arg = *num_arg;
}

void agent_hdr_set_int(agent *a, int int_arg, int int_arg2)
{
	a->shdr.int_arg = int_arg;
	a->shdr.int_arg2 = int_arg2;
}


int agent_hdr_set_str(agent *a, const char *str_arg)
{
	if (str_arg) {
		const size_t length = strlen(str_arg);
		assert(length < sizeof(a->shdr.str_arg));
		if (length >= sizeof(a->shdr.str_arg))
			return 1;
		strncpy(a->shdr.str_arg, str_arg, sizeof(a->shdr.str_arg) - 1);
	} else {
		memset(a->shdr.str_arg, 0, sizeof(a->shdr.str_arg));
	}

	return AGENT_OK;
}


int agent_hdr_set_bin_str(agent *a, const char *str_arg, size_t length)
{
	if (str_arg) {
		assert(length < sizeof(a->shdr.str_arg));
		if (length >= sizeof(a->shdr.str_arg))
			return 1;
		memcpy(a->shdr.str_arg, str_arg, length);
	} else {
		memset(a->shdr.str_arg, 0, sizeof(a->shdr.str_arg));
	}
	

	return AGENT_OK;
}


int agent_query(agent *a, int request)
{
	int ret;

	/* Prepare header struct; 
	 * don't touch alternate parameters */
	a->shdr.type = request;
	ret = agent_hdr_send(a);
	if (ret != 0) {
		a->error = 1;
		return ret;
	}

	/* Might hang? */
	ret = agent_hdr_recv(a);
	if (ret != 0) {
		a->error = 1;
		return ret;
	}

	return a->rhdr.status;
}

void agent_hdr_debug(const struct agent_header *hdr)
{
	printf("{Proto: %08X Type: %d Status: %d IArg: %d SArg: %s NArg: ", 
	       hdr->protocol_version, hdr->type, hdr->status, hdr->int_arg, hdr->str_arg);
	num_print_dec(hdr->num_arg);
	printf("}\n");
}

