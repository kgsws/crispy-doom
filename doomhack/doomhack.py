#!/usr/bin/python3
from os import listdir
from os import path
from struct import unpack
import sys
import cbor
import binascii

sounds = []
states = []
things = {}
weapons = {}
ammos = {}

output_file = "doomhack.lmp"

###

class kgTextParser:
	def __init__(self, file):
		self.f = open(file)

	def __del__(self):
		self.f.close()

	def get_line(self):
		# get line from file
		temp = self.f.readline()
		if not temp:
			return
		# split using whitespaces, keep strings
		# it's a mess, but it works
		ret = []
		text = ""
		have_whitespace = True
		in_string = False
		is_escaped = False
		for c in temp:
			if c == '\r':
				break
			if c == '\n':
				break

			if have_whitespace:
				if c.isspace():
					continue
				else:
					have_whitespace = False
					if len(text):
						ret.append(text)
					text = ""

			if in_string:
				if is_escaped:
					is_escaped = False
					if c == 't':
						text += '\t'
					elif c == 'n':
						text += '\n'
					else:
						text += c
				else:
					if c == '\\':
						is_escaped = True
					else:
						if c == '"':
							in_string = False
						text += c
			else:
				if c == '"':
					in_string = True
					text += c # mark this as a string
				elif c.isspace():
					have_whitespace = True
				else:
					text += c

		if len(text):
			ret.append(text)

		# done
		return ret

###

def get_files(dirname):
	filelist = [f for f in listdir(dirname) if path.isfile(path.join(dirname, f)) and path.splitext(f)[1] == ".txt"]
	return filelist

def parse_arg(atext):
	if atext[0] == '"':
		# string
		if atext[-1] != '"':
			raise Exception("Unterminated string '%s'!" % atext)
		return atext[1:-1]
	elif "." in atext:
		# float to fixed
		return int(float(atext) * 65536)
	elif atext == "true":
		return True
	elif atext == "false":
		return False
	else:
		# integer, autodetect
		return int(atext, 0)

def parse_textdef(file):
	global things
	global weapons
	global states
	global last_state

	# open parser
	tp = kgTextParser(file)

	while True:
		# get info
		text = tp.get_line()
		if not text:
			if type(text) == list:
				continue
			break

		last_state_idx = 0xFFFFFF
		anim_state = len(states)
		first_state = len(states)

		def_props = {}
		def_flags = []
		def_type = text[0]
		def_name = text[1]
		if len(text) > 2:
			def_id = int(text[2])
		else:
			def_id = False

		if def_type == "sound":
			sounds.append(def_name)
			continue

		if def_type != "actor" and def_type != "weapon" and def_type != "ammo":
			raise Exception("Unknown type '%s'!" % text[0])

		# start
		text = tp.get_line()
		if text[0] != '{':
			break

		# parse properties
		while True:
			text = tp.get_line()
			if not text:
				break
			if text[0] == '}':
				break
			# states
			if text[0].lower() == "states":
				if def_type == "ammo":
					raise Exception("Ammo should not have any states!")
				# start
				text = tp.get_line()
				if text[0] != '{':
					break
				# parse
				while True:
					text = tp.get_line()
					if text[0] == '}':
						break
					# check type
					if text[0][-1] == ':':
						# state label
						name = "State" + text[0][:-1]
						anim_state = len(states)
						def_props[name] = anim_state
					elif len(text) < 3 or text[2] == '+':
						# special keyword
						spec = text[0].lower()
						if spec == "stop":
							if last_state_idx < len(states):
								# can't use zero here!
								# this will be removed later
								states[last_state_idx]["Next"] = False
						elif spec == "loop":
							if last_state_idx < len(states):
								states[last_state_idx]["Next"] = anim_state
						elif spec == "wait":
							if last_state_idx < len(states):
								states[last_state_idx]["Next"] = last_state_idx
						elif spec == "goto":
							if text[1] == "LightDone":
								# special jump
								states[last_state_idx]["Next"] = -1
							else:
								# add temporary state jump info
								the_next = {}
								the_next["name"] = text[1]
								if len(text) > 2:
									the_next["offset"] = int(text[3])
								else:
									the_next["offset"] = 0
								if last_state_idx < len(states):
									states[last_state_idx]["Next"] = the_next
						elif spec == "state":
							states[last_state_idx]["Next"] = int(text[1])
						else:
							raise Exception("What is '%s'?" % text[0])
					else:
						act_idx = 3
						is_bright = 0
						action = False
						args = False

						# get sprite
						if len(text[0]) != 4:
							raise Exception("Bad sprite '%s'!" % text[0])
						sprite = text[0].upper()
						sprite = ord(sprite[0]) + ord(sprite[1]) * 0x100 + ord(sprite[2]) * 0x10000 + ord(sprite[3]) * 0x1000000

						tics = int(text[2])

						# check for more stuff
						if len(text) > act_idx:
							# bright sprite
							if text[act_idx].lower() == "bright":
								act_idx += 1
								is_bright = 32768
							# action and args
							if len(text) > act_idx:
								# action name
								action = text[act_idx]
								# args
								atmp = text[act_idx+1:]
								if len(atmp):
									args = {}
									# parse arguments
									for atxt in atmp:
										aarg = atxt.partition("=")
										if aarg[1] != "=":
											raise Exception("Bad action '%s' arg '%s'!" % (text[0], aarg))
										# handle argument type
										args[aarg[0]] = parse_arg(aarg[2])
						# add all frames
						for frame in text[1].upper():
							last_state_idx = len(states)
							state = {}
							state["Sprite"] = sprite
							state["Frame"] = (ord(frame) - 0x41) | is_bright
							state["Tics"] = tics
							state["Next"] = last_state_idx + 1
							if type(action) == str:
								state["Action"] = action
							if type(args) == dict:
								state["Args"] = args
							states.append(state)
			# copy anything found
			elif text[0][0] == '+':
				# flag
				flag_name = text[0][1:].lower()
				def_flags.append(flag_name)
			else:
				# prop
				def_props[text[0]] = parse_arg(text[1])

		# process states
		last_state = len(states)
		for idx in range(first_state, last_state):
			# state jumps (next state)
			if type(states[idx]["Next"]) == dict:
				new_next = False
				info = states[idx]["Next"]
				name = "State" + info["name"]
				new_next = def_props[name]
				if new_next >= 0:
					new_next += info["offset"]
				if new_next >= last_state:
					raise Exception("Invalid state jump '%s + %u'!" % (info["name"], info["offset"]))
				states[idx]["Next"] = new_next
			# no next state (stop)
			if type(states[idx]["Next"]) == bool:
				del states[idx]["Next"]
			# special argumets
			if "Args" in states[idx]:
				for key in states[idx]["Args"]:
					# anything starting with 'State' is considered state jump
					value = states[idx]["Args"][key]
					if key.startswith("State") and type(value) == str:
						value = "State" + value
						states[idx]["Args"][key] = def_props[value]

		if len(def_flags):
			def_props["Flags"] = def_flags

		# finalise
		if def_type == "actor":
			if type(def_id) == int:
				def_props["ID"] = def_id
			things[def_name] = def_props
		elif def_type == "weapon":
			if type(def_id) == int:
				def_props["Slot"] = def_id
			weapons[def_name] = def_props
		elif def_type == "ammo":
			ammos[def_name] = def_props

	del tp

# get text file list
files = get_files(".")

# parse all text files
for name in files:
	parse_textdef(name)

# generate root
root = {}
if len(sounds):
	root["sounds"] = sounds
if len(ammos):
	root["ammos"] = ammos
if len(weapons):
	root["weapons"] = weapons
if len(things):
	root["things"] = things
if len(states):
	root["states"] = states

print(root)

# export cbor
output = cbor.dumps(root)
with open(output_file,"wb") as f:
	f.write(output)

