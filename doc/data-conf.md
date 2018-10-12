# Configuration reader

Data format:

	# one-line comment
	// one-line comment
	/*
	multi-line comment
	*/

	# A key or value MAY be enclosed in quotes
	#  but MUST be enclosed in quotes if it contains a non-name characters (name regexp: "a-zA-Z_0-9")
	#  or is empty ("")
	# A key may have multiple values divided by whitespace.
	# Whitespace around a key or value is trimmed,
	#  but whitespace within quotes is preserved.
	key value_1 "value-2"

	# Contexts can be nested if enclosed in {}
	# '{' MUST be on the same line
	# '}' MUST be on a new line
	key {
		key "value"
	}

	key "value" {
		key "value"
	}

A configuration reader may support accessing nested objects via dot ('.'):

	# Set value for a 2nd-level object
	key1.key2 "value"
