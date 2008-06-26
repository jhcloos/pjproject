# $Id$
import time
import imp
import sys
import inc_const as const
from inc_cfg import *

# Load configuration
cfg_file = imp.load_source("cfg_file", sys.argv[2])

# Check media flow between ua1 and ua2
def check_media(ua1, ua2):
	ua1.send("#")
	ua1.send("1122")
	ua2.expect(const.RX_DTMF + "1")
	ua2.expect(const.RX_DTMF + "1")
	ua2.expect(const.RX_DTMF + "2")
	ua2.expect(const.RX_DTMF + "2")


# Test body function
def test_func(t, user_data):
	callee = t.process[0]
	caller = t.process[1]

	# if have_reg then wait for couple of seconds for PUBLISH
	# to complete (just in case pUBLISH is used)
	if callee.inst_param.have_reg:
		time.sleep(1)
	if caller.inst_param.have_reg:
		time.sleep(1)

	# Caller making call
	caller.send("m")
	caller.send(t.inst_params[0].uri)
	caller.expect(const.STATE_CALLING)
	
	# Callee waits for call and answers with 180/Ringing
	time.sleep(1)
	callee.expect(const.EVENT_INCOMING_CALL)
	callee.send("a")
	callee.send("180")
	callee.expect("SIP/2.0 180")
	caller.expect("SIP/2.0 180")

	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# Callee answers with 200/OK
	callee.send("a")
	callee.send("200")

	# Wait until call is connected in both endpoints
	time.sleep(1)
	caller.expect(const.STATE_CONFIRMED)
	callee.expect(const.STATE_CONFIRMED)

	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()
	time.sleep(1)
	caller.sync_stdout()
	callee.sync_stdout()

	# Test that media is okay
	time.sleep(2)
	check_media(caller, callee)
	check_media(callee, caller)

	# Hold call by caller
	caller.send("H")
	caller.sync_stdout()
	caller.expect(const.MEDIA_HOLD)
	callee.expect(const.MEDIA_HOLD)
	
	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# Release hold
	time.sleep(2)
	caller.send("v")
	caller.sync_stdout()
	callee.expect(const.MEDIA_ACTIVE)
	caller.expect(const.MEDIA_ACTIVE)

	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# Test that media is okay
	check_media(caller, callee)
	check_media(callee, caller)

	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# Hold call by callee
	callee.send("H")
	callee.sync_stdout()
	caller.expect(const.MEDIA_HOLD)
	callee.expect(const.MEDIA_HOLD)
	
	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# Release hold
	time.sleep(2)
	callee.send("v")
	callee.sync_stdout()
	caller.expect(const.MEDIA_ACTIVE)
	callee.expect(const.MEDIA_ACTIVE)

	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# Test that media is okay
	check_media(caller, callee)
	check_media(callee, caller)

	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# UPDATE (by caller)
	caller.send("U")
	caller.sync_stdout()
	callee.expect(const.MEDIA_ACTIVE)
	caller.expect(const.MEDIA_ACTIVE)
	
	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# Test that media is okay
	time.sleep(2)
	check_media(caller, callee)
	check_media(callee, caller)

	# UPDATE (by callee)
	callee.send("U")
	callee.sync_stdout()
	caller.expect(const.MEDIA_ACTIVE)
	callee.expect(const.MEDIA_ACTIVE)
	
	# Synchronize stdout
	caller.sync_stdout()
	callee.sync_stdout()

	# Test that media is okay
	time.sleep(2)
	check_media(caller, callee)
	check_media(callee, caller)

	# Hangup call
	time.sleep(1)
	caller.send("h")
	caller.sync_stdout()

	# Wait until calls are cleared in both endpoints
	caller.expect(const.STATE_DISCONNECTED)
	callee.expect(const.STATE_DISCONNECTED)
	

# Here where it all comes together
test = cfg_file.test_param
test.test_func = test_func

