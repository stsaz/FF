# Graphical UI

`FF/gui/winapi.h` has a set of functions written on top of Win32 API to work with graphical UI in Windows.  It requires you to call many functions to simply create a new window and fill it with controls, each having its own set of properties.  A GUI loader `FF/gui/loader.h` helps to automate this task: it reads a specially formatted text file (with an extension ".ffgui") and calls appropriate functions from `winapi.h`.  You'll have the complete windows with all their menus and controls as a result of this processing, not having written hundreds of calls to Win32 API by yourself.

## Format of `.ffgui` file

An ".ffgui" file is a text structural data that defines all UI elements and their properties.  It's similar to JSON, but it doesn't require you to put all text data within quotes.

* `list:[]`
	Multiple values can be specified, in any order.  `list:[]` must be omitted in the real .ffgui file.

* `any:[]`
	A single value can be specified.  `any:[]` must be omitted in the real .ffgui file.

* `NAME`
	String, defined by developer, that represents the name of UI element.
	It's better not to use quotes for the element names.

* `ACTION`
	String, defined by developer, that represents the command or action to be taken when an associated event from GUI is received.
	It's better not to use quotes for action names.

* `STRING`
	Arbitrary string that represents the text or value for an UI element.
	It's better to always use quotes for these values.

* `COLOR`
	Color in RGB notation ("#rrggbb") or by name (aqua black blue fuchsia green grey lime maroon navy olive orange purple red silver teal white yellow).

Scheme:

	# comment
	/*
	multi-line comment
	*/

	menu NAME {
		# Menu item with text
		# If the text is "-", this is a separator
		item STRING {

			# This item opens a submenu
			submenu NAME

			style list:[checked default disabled radio]
			action ACTION

			# This hotkey will be automatically registered upon parent window creation
			hotkey "Ctrl+Shift+Alt+K"
		}
	}

	window NAME {

		title STRING
		position X Y WIDTH HEIGHT
		placement INTEGER X Y WIDTH HEIGHT
		opacity INTEGER

		# If set, stick to screen borders.  The value is in pixels.
		borderstick INTEGER

		style list:[popup visible]

		# Parent window
		parent NAME

		onclose ACTION

		icon {
			filename STRING
			resource INTEGER
			index INTEGER
			size WIDTH HEIGHT
		}

		font {
			name STRING
			height INTEGER
			style list:[bold italic underline]
		}

		mainmenu NAME {
			item {} # see menu.item
		}

		label NAME {
			text STRING
			style list:[visible]
			font {}
			color COLOR
			cursor any:[hand]
			position X Y WIDTH HEIGHT

			onclick ACTION
		}

		image NAME {
			style list:[visible]
			position X Y WIDTH HEIGHT
			icon {}

			onclick ACTION
		}

		# Edit box
		editbox NAME {
			text STRING
			style list:[visible password readonly]
			font {}
			position X Y WIDTH HEIGHT

			onchange ACTION
		}

		# Multiline text box
		text NAME {} # see editbox

		combobox NAME {
			text STRING
			style list:[visible]
			font {}
			position X Y WIDTH HEIGHT
		}

		button NAME {
			text STRING
			style list:[visible]

			# Set font.  Don't use together with "icon".
			font {}

			# Set icon.  Don't use together with "font".
			icon {}

			position X Y WIDTH HEIGHT
			tooltip STRING

			action ACTION
		}

		checkbox NAME {
			text STRING
			style list:[checked visible]
			font {}
			position X Y WIDTH HEIGHT
			tooltip STRING

			action ACTION
		}

		radiobutton NAME {
			text STRING
			style list:[checked visible]
			font {}
			position X Y WIDTH HEIGHT
			tooltip STRING

			action ACTION
		}

		trackbar NAME {
			style list:[visible no_ticks both]
			position X Y WIDTH HEIGHT
			range
			value
			page_size

			onscroll ACTION
			onscrolling ACTION
		}

		progressbar NAME {
			style
			position X Y WIDTH HEIGHT
		}

		tab NAME {
			style list:[multiline fixed-width visible]
			position X Y WIDTH HEIGHT
			font {}

			onchange ACTION
		}

		listview NAME {
			style list:[checkboxes edit_labels explorer_theme full_row_select grid_lines has_buttons has_lines multi_select track_select visible]
			position X Y WIDTH HEIGHT
			color COLOR
			bgcolor COLOR
			popupmenu NAME

			chsel ACTION
			lclick ACTION
			dblclick ACTION
			oncheck ACTION

			column {
				width INTEGER
				align any:[left | right | center]
				order
			}
		}

		treeview NAME {
			style list:[checkboxes edit_labels explorer_theme full_row_select grid_lines has_buttons has_lines multi_select track_select visible]
			position X Y WIDTH HEIGHT
			color COLOR
			bgcolor COLOR
			popupmenu NAME
			chsel ACTION
		}

		paned NAME {
			child {
				move list:[x y]
				resize list:[cx cy]
			}
		}

		trayicon NAME {
			style list:[visible]
			icon {}
			popupmenu NAME
			lclick ACTION
		}

		statusbar NAME {
			style list:[visible]
			parts INTEGER...
		}

	}

	dialog {
		title STRING
		filter STRING
	}


## Using ".ffgui" file

Declare structures for your objects:

	// Main window
	struct gui_wmain {
		ffui_wnd wmain;
		ffui_btn bclose;
	};
	static const ffui_ldr_ctl wmain_ctls[] = {
		FFUI_LDR_CTL(struct gui_wmain, wmain),
		FFUI_LDR_CTL(struct gui_wmain, bclose),
		FFUI_LDR_CTL_END
	};

	// Top level elements
	struct gui {
		ffui_menu mfile;
		struct gui_wmain wmain;
	};
	static const ffui_ldr_ctl top_ctls[] = {
		FFUI_LDR_CTL(struct gui, mfile),

		FFUI_LDR_CTL3(struct gui, wmain, wmain_ctls),
		FFUI_LDR_CTL_END
	};

Now define callbacks for GUI loader:

	// This function is called by GUI loader each time it needs to get a pointer
	//  to a UI element by its name.
	static void* myapp_getctl(void *udata, const ffstr *name)
	{
		struct gui *g = udata;
		return ffui_ldr_findctl(top_ctls, g, name);
	}

	// This function is called by GUI loader each time it needs to get a command ID
	//  by its name.
	static int myapp_getcmd(void *udata, const ffstr *name)
	{
		if (ffs_eqz(name, "QUIT"))
			return A_QUIT;
		return 0;
	}

Initialize GUI:

	ffui_init();
	ffui_wnd_initstyle();

Initialize your objects:

	struct gui *g = ...;

Initialize GUI loader:

	ffui_loader ldr;
	ffui_ldr_init2(&ldr, &myapp_getctl, &myapp_getcmd, g);

Finally, execute:

	int r = ffui_ldr_loadfile(&ldr, "myapp.ffgui");
	if (r != 0)
		printf("error: %s\n", ffui_ldr_errstr(&ldr));
	ffui_ldr_fin(&ldr);

As a result, you'll have all your `struct gui*` objects filled in and ready for use.
