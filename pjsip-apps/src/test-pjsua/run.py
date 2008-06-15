# $Id:$
import sys
import imp
import re
import subprocess
import time

import inc_const as const

# Defaults
G_EXE="..\\..\\bin\\pjsua_vc6d.exe"

###################################
# TestError exception
class TestError:
	desc = ""
	def __init__(self, desc):
		self.desc = desc

###################################
# Poor man's 'expect'-like class
class Expect:
	proc = None
	echo = False
	trace_enabled = False
	name = ""
	inst_param = None
	rh = re.compile(const.DESTROYED)
	ra = re.compile(const.ASSERT, re.I)
	rr = re.compile(const.STDOUT_REFRESH)
	def __init__(self, inst_param):
		self.inst_param = inst_param
		self.name = inst_param.name
		self.echo = inst_param.echo_enabled
		self.trace_enabled = inst_param.trace_enabled
		fullcmd = G_EXE + " " + inst_param.arg + " --stdout-refresh=5 --stdout-refresh-text=" + const.STDOUT_REFRESH
		self.trace("Popen " + fullcmd)
		self.proc = subprocess.Popen(fullcmd, bufsize=0, stdin=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines=True)
	def send(self, cmd):
		self.trace("send " + cmd)
		self.proc.stdin.writelines(cmd + "\n")
	def expect(self, pattern, raise_on_error=True):
		self.trace("expect " + pattern)
		r = re.compile(pattern, re.I)
		refresh_cnt = 0
		while True:
			line = self.proc.stdout.readline()
		  	if line == "":
				raise TestError(self.name + ": Premature EOF")
			# Print the line if echo is ON
			if self.echo:
				print self.name + ": " + line,
			# Trap assertion error
			if self.ra.search(line) != None:
				if raise_on_error:
					raise TestError(self.name + ": " + line)
				else:
					return None
			# Count stdout refresh text. 
			if self.rr.search(line) != None:
				refresh_cnt = refresh_cnt+1
				if refresh_cnt >= 6:
					self.trace("Timed-out!")
					if raise_on_error:
						raise TestError(self.name + ": Timeout expecting pattern: " + pattern)
					else:
						return None		# timeout
			# Search for expected text
			if r.search(line) != None:
				return line

	def sync_stdout(self):
		self.trace("sync_stdout")
		self.send("echo 1")
		self.expect("echo 1")

	def wait(self):
		self.trace("wait")
		self.proc.wait()
	def trace(self, s):
		if self.trace_enabled:
			print self.name + ": " + "====== " + s + " ======"

#########################
# Error handling
def handle_error(errmsg, t):
	print "====== Caught error: " + errmsg + " ======"
	time.sleep(1)
	for p in t.process:
		p.send("q")
		p.send("q")
		p.expect(const.DESTROYED, False)
		p.wait()
	print "Test completed with error: " + errmsg
	sys.exit(1)


#########################
# MAIN	

if len(sys.argv)!=3:
	print "Usage: run.py MODULE CONFIG"
	print "Sample:"
	print "  run.py mod_run.py scripts-run/100_simple.py"
	sys.exit(1)


# Import the test script
script = imp.load_source("script", sys.argv[1])  

# Validate
if script.test == None:
	print "Error: no test defined"
	sys.exit(1)

if len(script.test.inst_params) == 0:
	print "Error: test doesn't contain pjsua run descriptions"
	sys.exit(1)

# Instantiate pjsuas
print "====== Running " + script.test.title + " ======"
for inst_param in script.test.inst_params:
	try:
		# Create pjsua's Expect instance from the param
		p = Expect(inst_param)
		# Wait until registration completes
		if inst_param.have_reg:
			p.expect(inst_param.uri+".*registration success")
	 	# Synchronize stdout
		p.send("")
		p.expect(const.PROMPT)
		p.send("echo 1")
		p.send("echo 1")
		p.expect("echo 1")
		# add running instance
		script.test.process.append(p)

	except TestError, e:
		handle_error(e.desc, script.test)

# Run the test function
if script.test.test_func != None:
	try:
		script.test.test_func(script.test)
	except TestError, e:
		handle_error(e.desc, script.test)

# Shutdown all instances
time.sleep(2)
for p in script.test.process:
	# Unregister if we have_reg to make sure that next tests
	# won't wail
	if p.inst_param.have_reg:
		p.send("ru")
		p.expect(p.inst_param.uri+".*unregistration success")
	p.send("q")
	p.send("q")
	time.sleep(0.5)
	p.expect(const.DESTROYED, False)
	p.wait()

# Done
print "Test " + script.test.title + " completed successfully"
sys.exit(0)

