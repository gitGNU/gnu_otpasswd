/**********************************************************************
 * otpasswd -- One-time password manager and PAM module.
 * Copyright (C) 2009, 2010 by Tomasz bla Fortuna <bla@thera.be>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with otpasswd. If not, see <http://www.gnu.org/licenses/>.
 *
 **********************************************************************/

#include <stdlib.h>


#define DEBUG 1

#include "ppp.h" /* Error handling mostly */

#include "nls.h"
#include "agent_private.h"
#include "print.h"

int agent_connect(agent **a_out, const char *agent_executable)
{
	int ret = 1;
	/* in[1]  - stdout of agent;  
	 * out[0] - stdin of agent; 
	 * in/out - naming from perspective of parent
	 */
	int in[2] = {-1, -1};
	int out[2] = {-1, -1};
	agent *a;
	*a_out = NULL;

	/* Allocate memory */
	a = malloc(sizeof(*a));
	if (!a)
		return AGENT_ERR_MEMORY;


	/* Initialize basic fields */
	a->error = 0;
	a->s = NULL;
	a->shdr.protocol_version = AGENT_PROTOCOL_VERSION;

	/* Create pipes */
	if (pipe(in) != 0)
		goto cleanup;
		
	if (pipe(out) != 0)
		goto cleanup1;

	/* Verify that agent executable exists */
	if (agent_executable == NULL)
		agent_executable = "./agent_otp";

	a->pid = fork();

	if (a->pid == 0) {
		/* Prepare pipes */
		close(in[0]);
		close(out[1]);
		close(0);
		close(1);
		close(2);
		/* fd 1 - stdout, fd 0 - stdin */
		dup(out[0]);
		dup(in[1]);

		/* Execute agent */
		execl(agent_executable, agent_executable, NULL);

		/* Failure */
		exit(5);
	}

	/* Close not our ends of pipes */
	close(in[1]); 
	in[1] = -1;
	
	close(out[0]);	
	out[0] = -1;

	a->in = in[0];
	a->out = out[1];


	/* TODO: Handle some signal? SIGPIPE? */

	/* Read message sent by server to indicate correct initialization
	 * or any initialization problems */
	if (agent_wait(a) != 0) {
		ret = AGENT_ERR_SERVER_INIT;
		print(PRINT_ERROR, "Timeout while waiting for agent initialization frame.\n");
		goto cleanup1;
	} else {
		ret = agent_hdr_recv(a);
		if (ret != 0) {
			print(PRINT_ERROR, "Error while receiving initial header.\n");
			goto cleanup1;
		
		}
		agent_hdr_debug(&a->rhdr);
		if (a->rhdr.type != AGENT_REQ_INIT) {
			print(PRINT_ERROR, "Initial frame parsing error.\n");
			ret = AGENT_ERR_SERVER_INIT;
			goto cleanup1;
		}

		ret = a->rhdr.status;
		if (ret != 0) {
			print(PRINT_ERROR, "Agent failed to initialize correctly.\n");
			goto cleanup1;
		}
	}

	print(PRINT_NOTICE, "Agent connection initialized correctly\n");
	*a_out = a;
	return AGENT_OK;

cleanup1:
	if (in[0] != -1) close(in[0]);
	if (in[1] != -1) close(in[1]);
	if (out[0] != -1) close(out[0]);
	if (out[1] != -1) close(out[1]);

cleanup:
	free(a);
	return ret;
}


int agent_server(agent **a_out) 
{
	agent *a;
	*a_out = NULL;
	a = malloc(sizeof(*a));
	if (!a) {
		return AGENT_ERR_MEMORY;
	}

	a->s = NULL;
	a->shdr.protocol_version = AGENT_PROTOCOL_VERSION;

	a->in = 0;
	a->out = 1;

	a->pid = -1;
	a->error = 0;

	*a_out = a;
	return AGENT_OK;
}

int agent_disconnect(agent *a)
{
	int ret = 0;
	/* Send quit message */

	/* Wait for child to close? */

	/* Close descriptors  */
	if (a->in != -1)
		ret += close(a->in);
	if (a->out != -1)
		ret += close(a->out);

	/* Free memory */
	free(a);

	/* TODO: Free/clean state if not NULL */
	return ret;
}

const char *agent_strerror(int error)
{
	switch (error) {
	case AGENT_OK:
		return _("No error");
	case AGENT_ERR:
		return _("Generic agent error.");
	case AGENT_ERR_MEMORY:
		return _("Error while allocating memory.");
	case AGENT_ERR_SERVER_INIT:
		return _("Error during agent initialization.");
	case AGENT_ERR_PROTOCOL_MISMATCH:
		return _("Agent protocol mismatch. Reinstall software.");
	case AGENT_ERR_DISCONNECT:
		return _("Agent unexpectedly disconnected.");

	default:
		if (error >= 100 && error <= 2000)
			return _( ppp_get_error_desc(error) );
		return _( "Unknown error" );
	}
	return NULL;
}


int agent_key_generate(agent *a)
{
	return agent_query(a, AGENT_REQ_KEY_GENERATE);
}

int agent_key_remove(agent *a)
{
	return agent_query(a, AGENT_REQ_KEY_REMOVE);
}

int agent_key_store(agent *a)
{
	return agent_query(a, AGENT_REQ_KEY_STORE);
}

int agent_flag_set(agent *a, int flag)
{
	return agent_query(a, AGENT_REQ_FLAG_SET);
}

int agent_flag_clear(agent *a, int flag)
{
	return agent_query(a, AGENT_REQ_FLAG_CLEAR);
}

int agent_flag_check(agent *a, int flag)
{
	return agent_query(a, AGENT_REQ_FLAG_CHECK);
}


int agent_read_state(agent *a)
{
	return agent_query(a, AGENT_REQ_READ_STATE);
}

/*
int agent_get_key(const agent *a, char *key)
{
}

int agent_get_num(const agent *a, num_t *key, int type)
{
}

int agent_get_int(agent *a, int field, int *reply)
{
}

int agent_get_passcode(const agent *a, int field, char **reply) 
{
}
*/







/* Config query:
 * ret = agent_config_query_int(agent *a, int field, int *reply);
 * ret = agent_config_query_str(agent *a, int field, char **reply); 
 */

int agent_testcase(void)
{
	int ret;
	int failures = 0;
	/* Create some messages and check if they are parsed accordingly */

	agent *a = NULL;

	ret = agent_connect(&a, NULL);
	if (ret != AGENT_OK) {
		printf("Unable to connect to agent.\n");
		puts(agent_strerror(ret));
		failures++;
		goto end;
	}

	// superuser only: agent_set_user(a, "user");

	/* Gets all information possible about state. Should be done 
	 * before generating key to ask user about some details. */
	ret = agent_read_state(a);
	/* Check ret */
	
	/* Interface for generating key */
//	int flags = agent_get_int(a, AGENT_FLAGS);




//	num_t value;
//	ret = agent_get_num(ar, AGENT_NUM_LATEST_CARD, value);
	/* ret might be: AGENT_OK, AGENT_ERR_POLICY, AGENT_PPP_ERROR */




//cleanup1:
	agent_disconnect(a);
end:
	return failures;
}
