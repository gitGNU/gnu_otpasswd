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
 **********************************************************************/

#include "agent_private.h"
#include "security.h"

/***
 * Private helper functions
 ***/
enum request_flags {
	_NONE = 0,
	_LOAD = 1,
	_LOCK = 2,
	_STORE = 4,
	_REMOVE = 8,
	_KEEP = 16, /* Keep after storing in memory */
};

/** Used to communicate how previous request was finished
 * without returning any important data (but the data might
 * be set before this command to sent it along)
 */
static int _send_reply(agent *a, int status) 
{
	agent_hdr_set_status(a, status);
	agent_hdr_set_type(a, AGENT_REQ_REPLY);
	int ret = agent_hdr_send(a);
	return ret;
}

/* Initialize state; possibly by loading from file  */
static int _state_init(agent *a, int flags)
{
	int ret;

	assert(a);
	assert(a->s == NULL);

	ret = ppp_state_init(&a->s, a->username);
	if (ret != 0) {
		return ret;
	}

	if (!(flags & _LOAD)) {
		/* Just initialize */
		a->new_state = 1;
		return 0;
	}

	a->new_state = 0;

	if (flags & _LOCK) {
		ret = ppp_state_load(a->s, 0);
	} else
		ret = ppp_state_load(a->s, PPP_DONT_LOCK);

	if (ret == STATE_NUMSPACE) {
		/* Loaded but we have no passcodes to use. Ignore at this level */
		ret = 0;
	}

	if (ret != 0) {
		ppp_state_fini(a->s);
		a->s = NULL;
	}

	print(PRINT_NOTICE, "State initialization done (%d)\n", ret);
	return ret;
}

/* Store if necessary, unlock and clean&free memory */
static int _state_fini(agent *a, int flags)
{
	int ret;
	assert(!((flags & _STORE) && (flags & _REMOVE))); /* Not at once */
	assert(a->s);

	if ((flags & _STORE) && (flags & _REMOVE))
		return AGENT_ERR;

	if (!a->s)
		return AGENT_ERR_NO_STATE;

	/* We store changes into the file
	 * We don't need to unlock just yet - ppp_fini
	 * will unlock state if it was locked
	 */
	int ppp_flags = 0;
	if (flags & _REMOVE)
		ppp_flags = PPP_REMOVE;
	else if (flags & _STORE)
		ppp_flags = PPP_STORE;

	/* If we have _KEEP flag we have to unlock manually */
	if (flags & _KEEP) {
		ppp_flags |= PPP_UNLOCK;
	}

	ret = ppp_state_release(a->s, ppp_flags);

	if (ret != 0) {
		print(PRINT_ERROR, 
		      "Error while saving state data. State not changed. [%d]\n", ret);
	}

	a->new_state = 0;
	
	if (!(flags & _KEEP)) {
		ppp_state_fini(a->s);
		a->s = NULL;
	}

	print(PRINT_NOTICE, "State finalization done (%d)\n", ret);
	return ret;
}

/* HELPER: Initialize for atomical operation with loading and locking.
 * Can be called for already loaded state. 
 * In case we are already creating new state (a->new_state is true)
 * this function does not toggle existing state.
 */
static int _state_init_atomical(agent *a) 
{
	int ret;

	if (a->new_state) {
		if (!a->s) {
			print(PRINT_ERROR, "New_state is enabled but state doesn't exists.\n");
			assert(0);
			return AGENT_ERR;
		}
		return AGENT_OK;
	}
	
	if (a->s) {
		/* Drop state if was loaded already */
		ret = _state_fini(a, _NONE);
		if (ret != AGENT_OK) {
			print(PRINT_ERROR, "Error while closing state before atomical operation. (%d)\n", ret);
			return ret;
		}
	}

	/* Load with locking */
	ret = _state_init(a, _LOAD | _LOCK);
	if (ret != AGENT_OK) {
		print(PRINT_WARN, "Error while loading state for atomical operation (%d)\n", ret);
	}
	return ret;
}

/* Pass it return value from previous statement and it will decide 
 * whether it can save state */
static int _state_fini_atomical(agent *a, int prev_ret) 
{
	int ret;

	if (a->new_state) {
		/* Do not finalize if during creation of new state,
		 * will be stored explicitly. */
		if (!a->s) {
			print(PRINT_ERROR, "New_state is enabled but state "
			      "doesn't exists while finalizing atomical operation.\n");
			assert(0);
			return AGENT_ERR;
		}
		return prev_ret;
	}

	if (prev_ret == 0) {
		return _state_fini(a, _STORE | _KEEP);
	} else {
		ret = _state_fini(a, _KEEP);
		print(PRINT_ERROR, "Error while finalizing atomical state after previous error (%d, then %d)\n",
		      prev_ret, ret);
		print(PRINT_ERROR, "%s ;; %s\n", agent_strerror(prev_ret), agent_strerror(ret));
		return prev_ret;
	}
}

static int request_verify_policy(agent *a, const cfg_t *cfg)
{
	/* Read request parameters */
	const int r_type = agent_hdr_get_type(a);
	const int r_int = agent_hdr_get_arg_int(a);

/*	const int r_status = agent_hdr_get_status(a);
	const num_t r_num = agent_hdr_get_arg_num(a);
	const char *r_str = agent_hdr_get_arg_str(a);
*/

	/* Most policy checking is done at this level. 
	 * Some, which might be more complicated can be done
	 * at PPP level, but then requires switches to allow
	 * root to circumvent policy at his will.
	 */
	const int privileged = security_is_privileged();

	switch (r_type) {
	case AGENT_REQ_USER_SET:
		/* Only administrator can select username */
		if (privileged)
			return AGENT_OK;
		else
			return AGENT_ERR_POLICY;

	case AGENT_REQ_DISCONNECT:
		return AGENT_OK;

	case AGENT_REQ_KEY_GENERATE:
		if (privileged)
			return AGENT_OK;

		if (cfg->key_generation == CONFIG_DISALLOW) {
			return AGENT_ERR_POLICY;
		}

		return AGENT_OK;

	case AGENT_REQ_KEY_REMOVE:
		if (privileged)
			return AGENT_OK;

		if (cfg->key_removal == CONFIG_DISALLOW) {
			return AGENT_ERR_POLICY;
		}
		return AGENT_OK;


	case AGENT_REQ_AUTHENTICATE:
		if (!privileged && cfg->shell_auth == CONFIG_DISALLOW)
			return AGENT_ERR_POLICY;
		return AGENT_OK;


		/* Validate if user doesn't already have a disabled state */
	case AGENT_REQ_STATE_NEW:
	{
		int ret;
		if (a->s) {
			/* This option is always run without reading state first */
			return AGENT_ERR_MUST_DROP_STATE;
		}

		if (privileged)
			return AGENT_OK;

		ret = _state_init(a, _LOAD);
		if (ret != 0) {
			print(PRINT_NOTICE, "User has no state so it can't be disabled or regenerated.\n");
			if (cfg->key_generation == CONFIG_DISALLOW) {
				print(PRINT_WARN, "Policy check failed: Disallowing key generation.\n");
				return AGENT_ERR_POLICY_GENERATION;
			}
			return AGENT_OK;
		}

		if (ppp_flag_check(a->s, FLAG_DISABLED) && cfg->disabling == CONFIG_DISALLOW) {
			ret = _state_fini(a, _NONE);
			if (ret != AGENT_OK) {
				print(PRINT_NOTICE, "Error during finalization: %s\n", agent_strerror(ret));
			}
			print(PRINT_WARN, "Policy check failed: Disallowing key regeneration with disabled state.\n");
			return AGENT_ERR_POLICY;
		}

		if (cfg->key_regeneration == CONFIG_DISALLOW) {
			ret = _state_fini(a, _NONE);
			if (ret != AGENT_OK) {
				print(PRINT_NOTICE, "Error during finalization: %s\n", agent_strerror(ret));
			}
			print(PRINT_WARN, "Policy check failed: Disallowing key regeneration.\n");
			return AGENT_ERR_POLICY_REGENERATION;
		}

		ret = _state_fini(a, _NONE);
		return AGENT_OK;
	}
		/* Those which doesn't require policy check */
	case AGENT_REQ_STATE_LOAD:
	case AGENT_REQ_STATE_STORE:
	case AGENT_REQ_STATE_DROP:
	case AGENT_REQ_GET_NUM:
	case AGENT_REQ_GET_INT:
	case AGENT_REQ_GET_WARNINGS:
	case AGENT_REQ_UPDATE_LATEST:
		return AGENT_OK;

	case AGENT_REQ_GET_STR:
		if ((r_int == PPP_FIELD_KEY || r_int == PPP_FIELD_COUNTER)
		    && cfg->key_print != CONFIG_ALLOW 
		    && !privileged)
			return AGENT_ERR_POLICY;
		else
			return AGENT_OK;

	case AGENT_REQ_GET_PASSCODE:
		if (!privileged && cfg->passcode_print == CONFIG_DISALLOW)
			return AGENT_ERR_POLICY;
		else
			return AGENT_OK;

	case AGENT_REQ_GET_PROMPT:
			return AGENT_OK;

	case AGENT_REQ_SKIP:
		if (!privileged && cfg->skipping == CONFIG_DISALLOW)
			return AGENT_ERR_POLICY;
		else
			return AGENT_OK;


	case AGENT_REQ_SET_NUM:
	case AGENT_REQ_SET_INT:
	case AGENT_REQ_SET_STR:
		return AGENT_OK;


	case AGENT_REQ_SET_SPASS:
		/* This is verified in the PPP along spass parameters */
		return AGENT_OK;

		/* FLAGS */
	case AGENT_REQ_FLAG_ADD:
		/* No one can change this: */
		if ((FLAG_SALTED & r_int) && cfg->salt == CONFIG_DISALLOW)
			return AGENT_ERR_POLICY_SALT;

		if (privileged) {
			return AGENT_OK;
		}

		/* Root can change this: */
		if (FLAG_DISABLED & r_int && cfg->disabling == CONFIG_DISALLOW)
			return AGENT_ERR_POLICY_DISABLED;

		if (FLAG_SHOW & r_int && cfg->show == CONFIG_DISALLOW)
			return AGENT_ERR_POLICY_SHOW;

		return AGENT_OK;

	case AGENT_REQ_FLAG_CLEAR:
		/* Check things even root shouldn't try (as PAM won't allow them) */
		if ((FLAG_SALTED & r_int) && cfg->salt == CONFIG_ENFORCE)
			return AGENT_ERR_POLICY_SALT;

		if (privileged) {
			return AGENT_OK;
		}
		/* Root can change this: */

		if (FLAG_DISABLED & r_int && cfg->disabling == CONFIG_DISALLOW)
			return AGENT_ERR_POLICY_DISABLED;

		if (FLAG_SHOW & r_int && cfg->show == CONFIG_ENFORCE)
			return AGENT_ERR_POLICY_SHOW;

		return AGENT_OK;


	case AGENT_REQ_FLAG_GET:
		return AGENT_OK;

	case AGENT_REQ_GET_ALPHABET:
		return AGENT_OK;

	default:
		print(PRINT_ERROR, "Unrecognized request type. (%d)\n", r_type);
		return AGENT_ERR;
	}

	return AGENT_ERR_POLICY;
}

static int request_execute(agent *a, const cfg_t *cfg)
{
	int ret;
	const int privileged = security_is_privileged();
	const int ppp_flags = privileged ? 0 : PPP_CHECK_POLICY;

	/* Read request parameters */
	const int r_type = agent_hdr_get_type(a);
	const int r_int = agent_hdr_get_arg_int(a);
	const int r_int2 = agent_hdr_get_arg_int2(a);
	const num_t r_num = agent_hdr_get_arg_num(a);
	const char *r_str = agent_hdr_get_arg_str(a);

	switch (r_type) {
	case AGENT_REQ_DISCONNECT:
		print(PRINT_NOTICE, "Executing (%d): Disconnect\n", r_type);
		/* Correct disconnect */
		/* Clear data */
		if (a->s) {
			ret = _state_fini(a, _NONE);
			if (ret != 0) {
				print(PRINT_WARN, 
				      "Error while handling finalizing "
				      "state during disconnect: %s\n",
				      agent_strerror(ret));
			}
		}
		return AGENT_REQ_DISCONNECT;

	case AGENT_REQ_USER_SET:
	{
		print(PRINT_NOTICE, "Executing (%d): User set\n", r_type);
		if (!r_str) {
			_send_reply(a, AGENT_ERR_REQ);
			break;
		}

		char *username = security_parse_user(r_str);
		if (!username) {
			_send_reply(a, AGENT_ERR_REQ_ARG);
			break;
		}

		agent_set_user(a, username);
		username = NULL;

		/* Clear state */
		if (a->s) {
			_state_fini(a, _NONE);
			a->s = NULL;
		}
		
		_send_reply(a, AGENT_OK);
		break;
	}

		/* STATE */
	case AGENT_REQ_STATE_NEW:
		print(PRINT_NOTICE, "Executing (%d): State new\n", r_type);
		if (a->s)
			return AGENT_ERR_MUST_DROP_STATE;

		ret = _state_init(a, _NONE);
		if (ret != 0) {
			print(PRINT_WARN, "Error while handling STATE_NEW: %s\n",
			      agent_strerror(ret));
			return ret;
		}
		_send_reply(a, ret);
		break;


	case AGENT_REQ_STATE_LOAD:
		print(PRINT_NOTICE, "Executing (%d): State load\n", r_type);
		if (a->s)
			return AGENT_ERR_MUST_DROP_STATE;

		/* Load without locking; we won't be able to save */
		ret = _state_init(a, _LOAD);
		if (ret != 0) {
			print(PRINT_WARN, "Error while handling STATE_LOAD: %s\n",
			      agent_strerror(ret));
		} 
		_send_reply(a, ret);
		break;

	case AGENT_REQ_STATE_STORE:
		print(PRINT_NOTICE, "Executing (%d): State store\n", r_type);
		ret = _state_fini(a, _STORE);
		if (ret != 0) {
			print(PRINT_WARN, "Error while handling STATE_STORE: %s\n",
			      agent_strerror(ret));
		}
		_send_reply(a, ret);
		break;

	case AGENT_REQ_STATE_DROP:
		print(PRINT_NOTICE, "Executing (%d): State drop\n", r_type);
		if (a->s) {
			ret = _state_fini(a, _NONE);
		} else {
			/* Nothing to drop */
			print(PRINT_WARN, "Unable to drop non-existant state\n");
			ret = AGENT_ERR_NO_STATE;
		}
		_send_reply(a, ret);
		break;


		/* KEY */
	case AGENT_REQ_KEY_GENERATE:
		print(PRINT_NOTICE, "Executing (%d): Key generate\n", r_type);
		if (!a->s) {
			print(PRINT_ERROR, "Must create new state first\n");
			_send_reply(a, AGENT_ERR_MUST_CREATE_STATE);
			break;
		}

		ret = ppp_key_generate(a->s, ppp_flags);
		
		if (ret != 0) {
			print(PRINT_ERROR, "Error while creating new key\n");
		} else {
			ret = AGENT_OK;
		}

		_send_reply(a, ret);
		break;

	case AGENT_REQ_KEY_REMOVE:
		print(PRINT_NOTICE, "Executing (%d): Key remove\n", r_type);
		if (a->s) {
			print(PRINT_ERROR, "Must drop state before removing it.\n");
			ret = AGENT_ERR_MUST_DROP_STATE;
			_send_reply(a, ret);	
			break;
		}

		/* Load state with locking */
		ret = _state_init(a, _LOAD | _LOCK);
		if (ret != 0) {
			print(PRINT_WARN, "Error while loading state for removal (%d)\n", ret);
			_send_reply(a, ret);
			break;
		}

		/* Remove state */
		ret = _state_fini(a, _REMOVE);
		if (ret != 0) {
			print(PRINT_ERROR, "Error while removing user state: %d\n", ret);
		}
		
		_send_reply(a, ret);
		break;

		/* FLAGS */
	case AGENT_REQ_FLAG_ADD:
	{
		print(PRINT_NOTICE, "Executing (%d): Flag add\n", r_type);
		ret = _state_init_atomical(a);
		if (ret == 0) {
			unsigned int new_flags;
			ppp_get_int(a->s, PPP_FIELD_FLAGS, &new_flags);
			new_flags |= r_int;
			ret = ppp_set_int(a->s, PPP_FIELD_FLAGS, new_flags, 
					  PPP_CHECK_POLICY);
			ret = _state_fini_atomical(a, ret);
			if (ret != 0) {
				print(PRINT_WARN, "Error while adding flags (%d).\n", 
				      ret);
			} else 
				ret = AGENT_OK;
		}
		_send_reply(a, ret);
		break;
	}

	case AGENT_REQ_FLAG_CLEAR:
	{
		print(PRINT_NOTICE, "Executing (%d): Flag clear\n", r_type);
		ret = _state_init_atomical(a);
		if (ret == 0) {
			unsigned int new_flags;
			ppp_get_int(a->s, PPP_FIELD_FLAGS, &new_flags);

			new_flags &= ~r_int;
			ret = ppp_set_int(a->s, PPP_FIELD_FLAGS, new_flags, 
					  PPP_CHECK_POLICY);
			ret = _state_fini_atomical(a, ret);
			if (ret != 0) {
				print(PRINT_WARN, 
				      "Error while clearing flags (%d).\n", ret);
			} else 
				ret = AGENT_OK;
		}
		_send_reply(a, ret);
		break;
	}
	
	case AGENT_REQ_FLAG_GET:
		print(PRINT_NOTICE, "Executing (%d): Flag get\n", r_type);
		print(PRINT_NOTICE, "Request: FLAG_GET\n");
		if (!a->s) {
			ret = AGENT_ERR_NO_STATE;
		} else {
			unsigned int flags;
			ppp_get_int(a->s, PPP_FIELD_FLAGS, &flags);
			agent_hdr_init(a, 0);
			agent_hdr_set_int(a, flags, 0);

			ret = AGENT_OK;			
		}

		_send_reply(a, ret);
		break;

	case AGENT_REQ_GET_NUM:
		print(PRINT_NOTICE, "Executing (%d): Get num\n", r_type);
		if (!a->s) {
			ret = AGENT_ERR_NO_STATE;
		} else {
			num_t tmp;
			ret = ppp_get_num(a->s, r_int, &tmp);
			if (ret != 0) {
				print(PRINT_ERROR, "Illegal num request.\n");
				ret = AGENT_ERR;
			} else {
				agent_hdr_init(a, 0);
				agent_hdr_set_num(a, &tmp);

				ret = AGENT_OK;
			}
		}
		_send_reply(a, ret);
		break;

	case AGENT_REQ_GET_INT:
		print(PRINT_NOTICE, "Executing (%d): Get int\n", r_type);
		if (!a->s) {
			ret = AGENT_ERR_NO_STATE;
		} else {
			unsigned int tmp;
			(void) ppp_get_int(a->s, r_int, &tmp);

			agent_hdr_init(a, 0);
			agent_hdr_set_int(a, tmp, 0);
			ret = AGENT_OK;
		}
		_send_reply(a, ret);
		break;

	case AGENT_REQ_GET_STR:
		print(PRINT_NOTICE, "Executing (%d): Get str\n", r_type);
		if (!a->s) {
			ret = AGENT_ERR_NO_STATE;
		} else {
			const char *tmp = NULL;
			(void) ppp_get_str(a->s, r_int, &tmp);

			/* This might fail because of length */
			agent_hdr_init(a, 0);
			if (r_int == PPP_FIELD_KEY)
				ret = agent_hdr_set_bin_str(a, tmp, 32);
			else
				ret = agent_hdr_set_str(a, tmp);
			if (ret != AGENT_OK) {
				print(PRINT_CRITICAL,
				      "Programmer error. Some str field (%d) "
				      "won't fit in agent structure.\n", r_int);
				ret = AGENT_ERR;
			} else {
				ret = AGENT_OK;
			}
		}
		_send_reply(a, ret);
		/* Immediately remove key from this part of memory */
		if (r_int == PPP_FIELD_KEY)
			agent_hdr_sanitize(a);
		break;

	case AGENT_REQ_GET_PASSCODE:
		if (!a->s) {
			/* This doesn't need to work atomically */
			ret = AGENT_ERR_NO_STATE;
		} else {
			agent_hdr_init(a, 0);
			char passcode[20];
			
			ret = ppp_get_passcode(a->s, r_num, passcode);
			if (ret == 0) {
				ret = agent_hdr_set_str(a, passcode);
				assert(ret == 0);
			}
		}

		_send_reply(a, ret);
		break;

	case AGENT_REQ_GET_PROMPT:
		print(PRINT_NOTICE, "Executing (%d): Get prompt\n", r_type);
		if (!a->s) {
			/* This doesn't need to work atomically */
			ret = AGENT_ERR_NO_STATE;
		} else {
			agent_hdr_init(a, 0);
			const char *prompt;
			
			prompt = ppp_get_prompt(a->s, 0, r_num);
			if (prompt != NULL) {
				ret = agent_hdr_set_str(a, prompt);
				assert(ret == 0);
			}
		}

		_send_reply(a, ret);
		break;

	case AGENT_REQ_GET_WARNINGS:
		/* Can work on already opened state */
		if (!a->s) {
			ret = AGENT_ERR_NO_STATE;
		} else {
			unsigned int failures = 0;
			int warnings = ppp_get_warning_conditions(a->s);
			if (warnings == PPP_ERROR) {
				_send_reply(a, AGENT_ERR);
				break;
			}

			ret = ppp_get_int(a->s, PPP_FIELD_RECENT_FAILURES, &failures);
			if (ret != 0) {
				ret = AGENT_ERR;
			}
			agent_hdr_set_int(a, warnings, failures);
			ret = AGENT_OK;
		}
		_send_reply(a, ret);
		break;

	case AGENT_REQ_UPDATE_LATEST:
	{
		/* Current value of 'latest card'. r_num has the proposed new value */
		num_t latest_card; 
		num_t current_card;
		print(PRINT_NOTICE, "Executing (%d): update latest\n", r_type);

		/* If already has some state check before reloading 
		 * We want to update latest card ONLY if is greater
		 * than latest_card. */
		if (a->s) {
			ret = ppp_get_num(a->s, PPP_FIELD_LATEST_CARD, 
					  &latest_card);
			ret = ppp_get_num(a->s, PPP_FIELD_CURRENT_CARD, 
					  &current_card);
			if (num_cmp(latest_card, r_num) > 0) {
				print(PRINT_NOTICE, "Current latest_card bigger than one which was supposed to be set.\n");
				_send_reply(a, AGENT_ERR_REQ_ARG);
				break;
			}

			/* Don't print also if LATEST_CARD is not NEXT to current_card 
			 * Or to the latest_printed card. So user can print somethings at random
			 * without mangling this variable.
			 */
			if (num_cmp(num_add_i(latest_card, 1), r_num) != 0 && 
			    num_cmp(num_add_i(current_card, 1), r_num) != 0) {

				print(PRINT_NOTICE, "Ignoring latest_card set; card number not adjacent to current.\n");
				_send_reply(a, AGENT_ERR_REQ_ARG);
				break;
			}
		}
		
		ret = _state_init_atomical(a);
		if (ret == 0) {
			ret = ppp_get_num(a->s, PPP_FIELD_LATEST_CARD, 
					  &latest_card);
			ret = ppp_get_num(a->s, PPP_FIELD_CURRENT_CARD, 
					  &current_card);

			if (num_cmp(latest_card, r_num) > 0) {
				print(PRINT_NOTICE, "Current latest_card bigger than one which was supposed to be set.\n");
				ret = AGENT_ERR_REQ_ARG;
			} else if (num_cmp(num_add_i(latest_card, 1), r_num) != 0 && 
				   num_cmp(num_add_i(current_card, 1), r_num) != 0) {
				/* Don't print also if LATEST_CARD is not NEXT to current_card */
				print(PRINT_NOTICE, "Ignoring latest_card set; card number not adjacent to current.\n");
				ret = AGENT_ERR_REQ_ARG;
			} else {
				ret = ppp_set_num(a->s, PPP_FIELD_LATEST_CARD, 
						  r_num, ppp_flags);
			}
			ret = _state_fini_atomical(a, ret);
			if (ret != 0) {
				print(PRINT_ERROR, "Error while updating latest card.\n");
			} else {
				ret = AGENT_OK;
			}
		}
		_send_reply(a, ret);
		break;
	}

	case AGENT_REQ_SKIP:
		print(PRINT_NOTICE, "Executing (%d): Skip\n", r_type);

		/* State must exist, but doesn't need to be already read. */
		if (!a->s) {
			ret = AGENT_ERR_NO_STATE;
		} else {
			ret = ppp_skip(a->s, r_num); /* This is done atomically */
			print(PRINT_NOTICE, "Skipping returned code %d\n", ret);
		}
		_send_reply(a, ret);
		break;

	case AGENT_REQ_AUTHENTICATE:
		print(PRINT_NOTICE, "Executing (%d): Authenticate\n", r_type);

		/* State must exist, but doesn't need to be already read. */
		if (!a->s) {
			ret = AGENT_ERR_NO_STATE;
		} else {
			ret = ppp_increment(a->s);
			if (ret == 0) {
				ret = ppp_authenticate(a->s, r_str); /* Done atomically */
				if (ret != 0) {
					print(PRINT_NOTICE, "CLI authentication failed.\n");
				}
			} else {
				print(PRINT_NOTICE, "Agent: ppp_increment failed.\n");
			}
		}
		_send_reply(a, ret);
		break;


	case AGENT_REQ_GET_ALPHABET:
	{
		print(PRINT_NOTICE, "Executing (%d): Get alphabet\n", r_type);
		const char *alphabet = NULL;
		agent_hdr_init(a, 0);

		ret = ppp_alphabet_get(r_int, &alphabet);

		int tmp = agent_hdr_set_str(a, alphabet);
		assert(tmp == AGENT_OK);

		_send_reply(a, ret);
		break;
	}

	case AGENT_REQ_SET_INT:
		print(PRINT_NOTICE, "Executing (%d): Set int\n", r_type);
		/* This sets PPP field: alphabet, codelength, but not flags. */
		ret = _state_init_atomical(a);
		if (ret == 0) {
			ret = ppp_set_int(a->s, r_int, r_int2, ppp_flags);
			print(PRINT_NOTICE, "SET_INT: FIELD=%d new value=%d flags=%d\n", r_int, r_int2, ppp_flags);
			ret = _state_fini_atomical(a, ret);
			if (ret != 0) {
				print(PRINT_ERROR, "Error while setting integer in state\n");
			} else {
				ret = AGENT_OK;
			}
		}
		_send_reply(a, ret);
		break;

	case AGENT_REQ_SET_STR:
		print(PRINT_NOTICE, "Executing (%d): Set str\n", r_type);
		ret = _state_init_atomical(a);
		if (ret == 0) {
			ret = ppp_set_str(a->s, r_int, r_str, ppp_flags);
			ret = _state_fini_atomical(a, ret);
			if (ret != 0) {
				print(PRINT_ERROR, "Error while setting string in state\n");
			} else {
				ret = AGENT_OK;
			}
		}
		_send_reply(a, ret);
		break;

	case AGENT_REQ_SET_NUM:
		print(PRINT_NOTICE, "Executing (%d): Set num; Not implemented\n", r_type);
		/* Not yet implemented. Is it required at all? */
		ret = AGENT_ERR;
		_send_reply(a, ret);
		break;
			
	case AGENT_REQ_SET_SPASS:
		print(PRINT_NOTICE, "Executing (%d): Set spass\n", r_type);
		ret = _state_init_atomical(a);
		if (ret == 0) {
			/* If r_int is true we want to REMOVE the spass not set it */
			int ret2 = ppp_set_spass(a->s, r_int ? NULL : r_str, ppp_flags);
			if (ret2 == PPP_ERROR_SPASS_SET || PPP_ERROR_SPASS_UNSET) {
				ret = _state_fini_atomical(a, 0);
				if (ret == 0) /* Get back to previous error value */
					ret = ret2;
			} else
				ret = _state_fini_atomical(a, ret2);
		} else {
			print(PRINT_ERROR, "Unable to read state!\n");
		}
		_send_reply(a, ret);
		break;

	default:
		print(PRINT_ERROR, "Unrecognized request type (%d).\n", r_type);
		return 1;
	}

	return AGENT_OK;
}


/***
 * Public interface used by agent.c
 ***/
int request_handle(agent *a) 
{
	int ret;

	cfg_t *cfg = cfg_get();
	assert(cfg);

	/* Wait for request, perform it and reply */
	ret = agent_hdr_recv(a);
	if (ret != 0) {
		print(PRINT_ERROR, "Client disconnected while waiting for request header (%d).\n", ret);
		return 1;
	}
		
	/* Verify policy */
	ret = request_verify_policy(a, cfg);
	switch (ret) {
	case AGENT_ERR_POLICY:
		_send_reply(a, AGENT_ERR_POLICY);
		return 0;
	case 0:
		break;
	default:
		/* Some strange error */
		_send_reply(a, ret);
		return ret;
	}

	/* This will send request reply itself */
	ret = request_execute(a, cfg);

	return ret;
}
