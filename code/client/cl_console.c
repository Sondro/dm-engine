/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// console.c

#include "client.h"

#define	DEFAULT_CONSOLE_LINE_WIDTH	78
#define	MAX_CONSOLE_WIDTH		150	//120

#define	NUM_CON_TIMES			4

#define	CON_TEXTSIZE			131072	//65536

typedef struct {
	qboolean	initialized;

	short		text[CON_TEXTSIZE];
	int			current;		// line where next message will be printed
	int			x;				// offset in current line for next print
	int			display;		// bottom of console displays this line

	int 		lineWidth;		// characters across screen
	int			totalLines;		// total lines in console scrollback
	int			totalUsedLines;	// total lines in use

	float		xAdjust;		// for wide aspect screens	// NOT USED AS OF YET

	float		displayFrac;	// aproaches finalFrac at con_speed
	float		finalFrac;		// 0.0 to 1.0 lines of console to display

	int			visLines;		// in scanlines

	int			times[NUM_CON_TIMES];	// cls.realtime time the line was generated
								// for transparent notify lines
	vec4_t		color;

	int			visWidth;
	int			visPage;

	qboolean	newLine;

	int			conWidth;			// actual horizontal bounds of the console window

} console_t;
console_t con;

chatModeEnum_t		chat_mode;
extern  int			chat_playerNum;

cvar_t *con_speed;
cvar_t *con_notifyTime;
cvar_t *con_notifyXOffset;
cvar_t *con_background;
cvar_t *con_backColor;
cvar_t *con_foreColor;

cvar_t *con_scrollBar;
cvar_t *con_scrollLines;
cvar_t *con_showClock;
cvar_t *con_showVersion;
cvar_t *con_width;

int con_field_width = DEFAULT_CONSOLE_LINE_WIDTH;

static const char *chatmode_text[CHATMODE_TOTAL] = {
	"chat: ", "team chat: ", "tell target: ", "tell attacker: "
};

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f( void ) {
	// Can't toggle the console when it's the only thing available
	if ( cls.state == CA_DISCONNECTED && Key_GetCatcher() == KEYCATCH_CONSOLE ) {
		return;
	}

	Field_Clear( &consoleField );
	consoleField.widthInChars = con_field_width;

	Con_ClearNotify();
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_CONSOLE );
}


/*
================
Con_Chat_f
================
*/
static void Con_Chat_f( void ) {
	chat_playerNum = -1;
	chat_mode = CHATMODE_ALL;
	Field_Clear( &chatField );
	chatField.widthInChars = ( con.conWidth - strlen(chatmode_text[CHATMODE_ALL]) * cls.bigchar_width ) / cls.bigchar_width;

	//Com_Printf( "widthInChars=%i\n", chatField.widthInChars );
	
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_ChatTeam_f
================
*/
static void Con_ChatTeam_f( void ) {
	chat_playerNum = -1;
	chat_mode = CHATMODE_TEAM;
	Field_Clear( &chatField );
	chatField.widthInChars = ( con.conWidth - strlen( chatmode_text[CHATMODE_TEAM] ) * cls.bigchar_width ) / cls.bigchar_width;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_TellTarget_f
================
*/
static void Con_TellTarget_f( void ) {
	chat_playerNum = cgvm ? VM_Call( cgvm, 0, CG_CROSSHAIR_PLAYER ) : -1;
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_mode = CHATMODE_TARGET;
	Field_Clear( &chatField );
	chatField.widthInChars = ( con.conWidth - strlen( chatmode_text[CHATMODE_TARGET] ) * cls.bigchar_width ) / cls.bigchar_width;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_TellAttacker_f
================
*/
static void Con_TellAttacker_f( void ) {
	chat_playerNum = cgvm ? VM_Call( cgvm, 0, CG_LAST_ATTACKER ) : -1;
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	Field_Clear( &chatField );
	chatField.widthInChars = ( con.conWidth - strlen( chatmode_text[CHATMODE_ATTACKER] ) * cls.bigchar_width ) / cls.bigchar_width;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
===============
Con_Fixup
===============
*/
void Con_Fixup( void ) {
	int filled;

	if ( con.current >= con.totalLines ) {
		filled = con.totalLines;
	}
	else {
		filled = con.current + 1;
	}

	if ( filled <= con.visPage ) {
		con.display = con.current;
	}
	else if ( con.current - con.display > filled - con.visPage ) {
		con.display = con.current - filled + con.visPage;
	}
	else if ( con.display > con.current ) {
		con.display = con.current;
	}
}


/*
================
Con_Clear_f
================
*/
static void Con_Clear_f( void ) {
	int		i;

	for ( i = 0; i < con.lineWidth; i++ ) {
		con.text[i] = ( ColorIndex( COLOR_WHITE ) << 8 ) | ' ';
	}

	con.x = 0;
	con.current = 0;
	con.newLine = qtrue;

	Con_Bottom();		// go to end
//dm
	Con_Fixup();
//-dm
}


/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
static void Con_Dump_f( void ) {
	int			l, x, i, n;
	short		*line;
	fileHandle_t	f;
	int			bufferlen;
	char		*buffer;
	char		filename[MAX_OSPATH];
	const char *ext;

	if ( Cmd_Argc() != 2 ) {
		PrintUsageDesc( "conDump", "<filename>", "Saves console text to file." );
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".txt" );

	if ( !FS_AllowedExtension( filename, qfalse, &ext ) ) {
		Com_Printf( S_COL_BASE "Invalid filename extension: " S_COL_VAL "%s\n", ext );
		return;
	}

	f = FS_FOpenFileWrite( filename );
	if ( f == FS_INVALID_HANDLE )
	{
		Com_WPrintf( "Couldn't open file for writing: " S_COL_VAL "%s\n", filename );
		return;
	}

	Com_Printf( S_COL_BASE "Dumped console text to " S_COL_VAL "%s" S_COL_BASE ".\n", filename );

	if ( con.current >= con.totalLines ) {
		n = con.totalLines;
		l = con.current + 1;
	}
	else {
		n = con.current + 1;
		l = 0;
	}

	bufferlen = con.lineWidth + ARRAY_LEN( Q_NEWLINE ) * sizeof( char );
	buffer = Hunk_AllocateTempMemory( bufferlen );

	// write the remaining lines
	buffer[bufferlen - 1] = '\0';

	for ( i = 0; i < n; i++, l++ )
	{
		line = con.text + ( l % con.totalLines ) * con.lineWidth;
		// store line
		for ( x = 0; x < con.lineWidth; x++ )
			buffer[x] = line[x] & 0xff;
		buffer[con.lineWidth] = '\0';
		// terminate on ending space characters
		for ( x = con.lineWidth - 1; x >= 0; x-- ) {
			if ( buffer[x] == ' ' )
				buffer[x] = '\0';
			else
				break;
		}
		Q_strcat( buffer, bufferlen, Q_NEWLINE );
		FS_Write( buffer, strlen( buffer ), f );
	}

	Hunk_FreeTempMemory( buffer );
	FS_FCloseFile( f );
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;

	for ( i = 0; i < NUM_CON_TIMES; i++ ) {
		con.times[i] = 0;
	}
}


/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize( void ) {
	int			i, j, width, oldWidth, oldtotalLines, oldCurrent, numLines, numChars;
	short		tbuf[CON_TEXTSIZE], *src, *dst;
	static		int old_width, old_vispage;
	int			visPage;
	static int conWidth = 0, conWidthMod = -1;
	static int conFieldMod = 0;		// track changes in con_field_width

	if ( con.visWidth == cls.glconfig.vidWidth && conFieldMod == con_field_width && conWidthMod == con_width->integer )
		return;
	
	con.visWidth = cls.glconfig.vidWidth;

	if ( cls.smallchar_width == 0 ) { // might happen on early init
		cls.smallchar_width = SMALLCHAR_WIDTH;
		cls.smallchar_height = SMALLCHAR_HEIGHT;
		cls.bigchar_width = BIGCHAR_WIDTH;
		cls.bigchar_height = BIGCHAR_HEIGHT;

		cls.vidScale = 1.0;
	}

	if ( cls.glconfig.vidWidth == 0 ) // video hasn't been initialized yet
	{
		con.conWidth = 0;
		width = DEFAULT_CONSOLE_LINE_WIDTH;
		con.lineWidth = width;
		con.totalLines = CON_TEXTSIZE / con.lineWidth;
		con.visPage = 4;

		Con_Clear_f();
	}
	else
	{
		conFieldMod = conWidth;
		conWidth = conWidthMod = con_width->integer;
		if ( !conWidth ) {
			conWidth = cls.glconfig.vidWidth;
		} else if ( conWidth < 0 ) {
			conWidth = (int)(640.0f * cls.vidScale);
		} else {
			if ( conWidth < 480 ) conWidth = 480;
		}
		if ( conWidth > cls.glconfig.vidWidth )
			conWidth = cls.glconfig.vidWidth;
		con.conWidth = conWidth;

		width = ( con.conWidth / cls.smallchar_width ) - 4;
		if ( width > MAX_CONSOLE_WIDTH )
			width = MAX_CONSOLE_WIDTH;
		consoleField.widthInChars = con_field_width = width;
		conFieldMod = con_field_width;

		//Com_Printf( S_COLOR_RED "console update, con_field_width=%i conWidth=%i con_width=%i\n", con_field_width, conWidth, con_width->integer );

		visPage = cls.glconfig.vidHeight / ( cls.smallchar_height * 2 ) - 1;

		if ( old_vispage == visPage && old_width == width )
			return;

		oldWidth = con.lineWidth;
		oldtotalLines = con.totalLines;
		oldCurrent = con.current;

		con.lineWidth = width;
		con.totalLines = CON_TEXTSIZE / con.lineWidth;
		con.visPage = visPage;

		old_vispage = visPage;
		old_width = width;

		numChars = oldWidth;
		if ( numChars > con.lineWidth )
			numChars = con.lineWidth;

		if ( oldCurrent > oldtotalLines )
			numLines = oldtotalLines;
		else
			numLines = oldCurrent + 1;

		if ( numLines > con.totalLines )
			numLines = con.totalLines;

		Com_Memcpy( tbuf, con.text, CON_TEXTSIZE * sizeof( short ) );

		for ( i = 0; i < CON_TEXTSIZE; i++ )
			con.text[i] = ( ColorIndex( COLOR_WHITE ) << 8 ) | ' ';

		for ( i = 0; i < numLines; i++ )
		{
			src = &tbuf[( ( oldCurrent - i + oldtotalLines ) % oldtotalLines ) * oldWidth];
			dst = &con.text[( numLines - 1 - i ) * con.lineWidth];
			for ( j = 0; j < numChars; j++ )
				*dst++ = *src++;
		}

		Con_ClearNotify();

		con.current = numLines - 1;
	}

	con.display = con.current;
}


/*
==================
Cmd_CompleteTxtName
==================
*/
void Cmd_CompleteTxtName( char *args, int argNum ) {
	if ( argNum == 2 ) {
		Field_CompleteFilename( "", "txt", qfalse, FS_MATCH_EXTERN | FS_MATCH_STICK );
	}
}


/*
================
Con_Init
================
*/
void Con_Init( void ) {
	con_notifyTime = Cvar_Get( "con_notifyTime", "3", CVAR_ARCHIVE_ND, "0", "10", CV_FLOAT );
	Cvar_SetDescription( con_notifyTime, "Time notifications are displayed for (in seconds.)" );
	con_speed = Cvar_Get( "con_speed", "3", CVAR_ARCHIVE_ND, "1", "10", CV_FLOAT );
	Cvar_SetDescription( con_speed, "Console opening/closing scroll speed." );

	con_notifyXOffset = Cvar_Get( "con_notifyXOffset", "0", CVAR_ARCHIVE_ND, "0", NULL, CV_INTEGER );
	Cvar_SetDescription( con_notifyXOffset, "Notifications X-offset." );
	con_background = Cvar_Get( "con_background", "1", CVAR_ARCHIVE_ND, "0", "2", CV_INTEGER );
	Cvar_SetDescription( con_background, "Draws the background shader.\n 0: Flat color, set by con_backColor\n 1: Normal background image\n 2: Colorized background image, set by con_backColor" );
	con_backColor = Cvar_Get( "con_backColor", "", CVAR_ARCHIVE_ND, NULL, NULL, CV_NONE );
	Cvar_SetDescription( con_backColor, "Console background color, set as R G B A values from 0-255." );
	con_foreColor = Cvar_Get( "con_foreColor", "255 0 0 255", CVAR_ARCHIVE_ND, NULL, NULL, CV_NONE );
	Cvar_SetDescription( con_foreColor, "Console info text and breaker color, set as R G B A values from 0-255." );
	con_scrollBar = Cvar_Get( "con_scrollBar", "1", CVAR_ARCHIVE_ND, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_scrollBar, "Draws the console scroll bar." );
	con_scrollLines = Cvar_Get( "con_scrollLines", "2", CVAR_ARCHIVE_ND, "1", "8", CV_INTEGER );
	Cvar_SetDescription( con_scrollLines, "Console line scroll factor." );
	con_showClock = Cvar_Get( "con_showClock", "0", CVAR_ARCHIVE_ND, "0", "2", CV_INTEGER );
	Cvar_SetDescription( con_showClock, "Show current time in console.\n 0: Hidden\n 1: 24 hour clock\n 2: AM/PM clock" );
	con_showVersion = Cvar_Get( "con_showVersion", "1", CVAR_ARCHIVE_ND, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_showVersion, "Show engine version information." );
	con_width = Cvar_Get( "con_width", "-1", CVAR_ARCHIVE_ND, "-1", NULL, CV_INTEGER );
	Cvar_SetDescription( con_width, "Console window width.\n -1: Scale from 640x480\n 0: Scale to video width\n 480..n: Set native width value" );

	Field_Clear( &consoleField );
	consoleField.widthInChars = con_field_width;

	Cmd_AddCommand( "clear", Con_Clear_f );
	Cmd_AddCommand( "conDump", Con_Dump_f );
	Cmd_SetCommandCompletionFunc( "conDump", Cmd_CompleteTxtName );
	Cmd_AddCommand( "toggleConsole", Con_ToggleConsole_f );
	Cmd_AddCommand( "messageMode", Con_Chat_f );
	Cmd_AddCommand( "messageMode2", Con_ChatTeam_f );
	Cmd_AddCommand( "messageMode3", Con_TellTarget_f );
	Cmd_AddCommand( "messageMode4", Con_TellAttacker_f );

	Cmd_AddCommand( "chat", Con_Chat_f );
	Cmd_AddCommand( "chatTeam", Con_ChatTeam_f );
	Cmd_AddCommand( "tellTarget", Con_TellTarget_f );
	Cmd_AddCommand( "tellAttacker", Con_TellAttacker_f );
}


/*
================
Con_Shutdown
================
*/
void Con_Shutdown( void ) {
	Cmd_RemoveCommand( "clear" );
	Cmd_RemoveCommand( "conDump" );
	Cmd_RemoveCommand( "toggleConsole" );
	Cmd_RemoveCommand( "messageMode" );
	Cmd_RemoveCommand( "messageMode2" );
	Cmd_RemoveCommand( "messageMode3" );
	Cmd_RemoveCommand( "messageMode4" );
	Cmd_RemoveCommand( "chat" );
	Cmd_RemoveCommand( "chatTeam" );
	Cmd_RemoveCommand( "tellTarget" );
	Cmd_RemoveCommand( "tellAttacker" );
}


/*
===============
Con_Linefeed

Move to newLine only when we _really_ need this
===============
*/
void Con_NewLine( void ) {
	short *s;
	int i;

	// follow last line
	if ( con.display == con.current )
		con.display++;
	con.current++;
	con.totalUsedLines = con.current;	// %con.totalLines;
	if ( con.totalUsedLines > con.totalLines )
		con.totalUsedLines = con.totalLines;
	s = &con.text[( con.current % con.totalLines ) * con.lineWidth];
	for ( i = 0; i < con.lineWidth; i++ )
		*s++ = ( ColorIndex( COLOR_WHITE ) << 8 ) | ' ';

	con.x = 0;
}


/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed( qboolean skipnotify ) {
	// mark time for transparent overlay
	if ( con.current >= 0 ) {
		if ( skipnotify )
			con.times[con.current % NUM_CON_TIMES] = 0;
		else
			con.times[con.current % NUM_CON_TIMES] = cls.realtime;
	}

	if ( con.newLine ) {
		Con_NewLine();
	} else {
		con.newLine = qtrue;
		con.x = 0;
	}

	Con_Fixup();
}


/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt ) {
	int			y;
	int			c, l;
	int			colorIndex;
	qboolean	skipNotify = qfalse;	// NERVE - SMF
	int			prev;					// NERVE - SMF

	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( txt, "[skipnotify]", 12 ) ) {
		skipNotify = qtrue;
		txt += 12;
	}

	// for some demos we don't want to ever show anything on the console
	if ( con_noPrint && con_noPrint->integer ) {
		return;
	}

	if ( !con.initialized ) {
		con.color[0] =
			con.color[1] =
			con.color[2] =
			con.color[3] = 1.0f;
		con.visWidth = -9999;
		Con_CheckResize();
		con.initialized = qtrue;
	}

	colorIndex = ColorIndex( COLOR_WHITE );

	while ( ( c = *txt ) != 0 ) {
		if ( Q_IsColorString( txt ) && *( txt + 1 ) != '\n' ) {
			colorIndex = ColorIndexFromChar( *( txt + 1 ) );
			txt += 2;
			continue;
		}

		// count word length
		for ( l = 0; l < con.lineWidth; l++ ) {
			if ( txt[l] <= ' ' ) {
				break;
			}
		}

		// word wrap
		if ( l != con.lineWidth && ( con.x + l >= con.lineWidth ) ) {
			Con_Linefeed( skipNotify );
		}

		txt++;

		switch ( c )
		{
		case '\n':
			Con_Linefeed( skipNotify );
			break;
		case '\r':
			con.x = 0;
			break;
		default:
			if ( con.newLine ) {
				Con_NewLine();
				Con_Fixup();
				con.newLine = qfalse;
			}
			// display character and advance
			y = con.current % con.totalLines;
			con.text[y * con.lineWidth + con.x] = ( colorIndex << 8 ) | ( c & 255 );
			con.x++;
			if ( con.x >= con.lineWidth ) {
				Con_Linefeed( skipNotify );
			}
			break;
		}
	}

	// mark time for transparent overlay
	if ( con.current >= 0 ) {
		if ( skipNotify ) {
			prev = con.current % NUM_CON_TIMES - 1;
			if ( prev < 0 )
				prev = NUM_CON_TIMES - 1;
			con.times[prev] = 0;
		}
		else {
			con.times[con.current % NUM_CON_TIMES] = cls.realtime;
		}
	}
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
static void Con_DrawInput( int x, int y ) {

	if ( cls.state != CA_DISCONNECTED && !( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return;
	}

	x += 1;

	re.SetColor( con.color );
	
	SCR_DrawSmallChar( x + cls.smallchar_width, y, ']' );

	Field_Draw( &consoleField, x + (int)(2 * cls.smallchar_width), y, consoleField.widthInChars - 1, qtrue, qfalse, qtrue );

	re.SetColor( NULL );
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify( void ) {
	int		x;
	short	*text;
	int		i;
	int		time;
	int		skip;
	int		currentColorIndex;
	int		colorIndex;
	int		ax, ay;

	currentColorIndex = ColorIndex( COLOR_WHITE );
	re.SetColor( g_color_table[currentColorIndex] );

	ax = ( cls.glconfig.vidWidth - con.conWidth ) / 2 + con_notifyXOffset->integer;
	ay = 0;

	x = 0;
	for ( i = con.current - NUM_CON_TIMES + 1; i <= con.current; i++ ) {
		if ( i < 0 )
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if ( time == 0 )
			continue;
		time = cls.realtime - time;
		if ( time >= con_notifyTime->value * 1000 )
			continue;
		text = con.text + ( i % con.totalLines ) * con.lineWidth;

		if ( cl.snap.ps.pm_type != PM_INTERMISSION && Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CGAME ) ) {
			continue;
		}

		for ( x = 0; x < con.lineWidth; x++ ) {
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}
			colorIndex = ( text[x] >> 8 ) & 63;
			if ( currentColorIndex != colorIndex ) {
				currentColorIndex = colorIndex;
				re.SetColor( g_color_table[colorIndex] );
			}
			SCR_DrawSmallChar( ax + ( x + 1 ) * cls.smallchar_width, ay, text[x] & 0xff );
		}

		ay += cls.smallchar_height;
	}

	re.SetColor( NULL );

	if ( Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CGAME ) ) {
		return;
	}

	// draw the chat line
	if ( Key_GetCatcher() & KEYCATCH_MESSAGE ) {

		SCR_DrawBigString( ax + cls.smallchar_width, ay, chatmode_text[chat_mode], 1.0f, qfalse );
		skip = strlen( chatmode_text[chat_mode] );

		Field_Draw( &chatField, ax + skip * cls.bigchar_width, ay,
			( ( con.conWidth - skip * cls.bigchar_width ) / cls.bigchar_width ) - 1, qtrue, qtrue, qtrue );
	}
}


/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( float frac ) {
	static float backColorValue[4] = { 0.0, 0.0, 0.0, 0.0 };
	static float foreColorValue[4] = { 0.0, 0.0, 0.0, 0.0 };

	// for tracking cvar changes
	static char  backColorString[MAX_CVAR_VALUE_STRING] = { '\0' };
	static char  foreColorString[MAX_CVAR_VALUE_STRING] = { '\0' };

	int		i;
	int		x, y;
	int		rows;
	short	*text;
	int		row;
	int		lines, linesBottom;
	int		currentColorIndex;
	int		colorIndex;
	int		yf, wf;
	int		ax, ay, breakerWidth, endHeight;
	char	*v[4];
#ifdef USE_CURL
	int		dlx1 = 0, dlx2 = 0;
#endif
	if ( re.FinishBloom )
		re.FinishBloom();

	lines = cls.glconfig.vidHeight * frac;
	if ( lines <= 0 )
		return;
	linesBottom = con_showVersion->integer || con_showClock->integer
#ifdef USE_CURL
		|| download.progress[0]
#endif
		? cls.smallchar_height : 0;
	lines -= linesBottom;

	if ( lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;
	endHeight = (int)( (float)cls.glconfig.vidHeight * 0.5f );

	wf = con.conWidth;
	yf = frac * cls.glconfig.vidHeight;

	ax = ( cls.glconfig.vidWidth - wf ) / 2;
	ay = 0;

	breakerWidth = (int)ceil(2.0f * cls.vidScale);

	//Com_Printf( "ax=%i ay=%i wf=%i yf=%i lines=%i linesBottom=%i, breakerWidth=%i smallchar_width=%i smallchar_height=%i\n", ax, ay, wf, yf, lines, linesBottom, breakerWidth, cls.smallchar_width, cls.smallchar_height );

	if ( con_foreColor->string[0] ) {
		char buf[MAX_CVAR_VALUE_STRING];
		// track changes
		if ( strcmp( con_foreColor->string, foreColorString ) ) {
			Q_strncpyz( foreColorString, con_foreColor->string, sizeof( foreColorString ) );
			Q_strncpyz( buf, con_foreColor->string, sizeof( buf ) );
			Com_Split( buf, v, 4, ' ' );
			for ( i = 0; i < 4; i++ ) {
				foreColorValue[i] = Q_atof( v[i] ) / 255.0f;
				if ( foreColorValue[i] > 1.0f ) {
					foreColorValue[i] = 1.0f;
				}
				else if ( foreColorValue[i] < 0.0f ) {
					foreColorValue[i] = 0.0f;
				}
			}
		}
	}

	if ( con_backColor->string[0] ) {
		char buf[MAX_CVAR_VALUE_STRING];
		// track changes
		if ( strcmp( con_backColor->string, backColorString ) ) {
			Q_strncpyz( backColorString, con_backColor->string, sizeof( backColorString ) );
			Q_strncpyz( buf, con_backColor->string, sizeof( buf ) );
			Com_Split( buf, v, 4, ' ' );
			for ( i = 0; i < 4; i++ ) {
				backColorValue[i] = Q_atof( v[i] ) / 255.0f;
				if ( backColorValue[i] > 1.0f ) {
					backColorValue[i] = 1.0f;
				}
				else if ( backColorValue[i] < 0.0f ) {
					backColorValue[i] = 0.0f;
				}
			}
		}
	}

	// draw the background
	if ( yf < 1 ) {
		yf = 0;
	} else {
		int backy = ay + yf - (int)( (float)cls.glconfig.vidHeight / 2.0f );
		if ( con_background->integer ) {
			re.SetColor( con_background->integer > 1 ? backColorValue : colorWhite );
			SCR_DrawPic( ax, backy, wf, endHeight, cls.consoleShader );
		} else {
			re.SetColor( backColorValue );
			SCR_DrawPic( ax, backy, wf, endHeight, cls.whiteShader );
		}
	}

#ifdef USE_CURL
	if ( download.progress[0] ) {
		int dx, dw;
		currentColorIndex = ColorIndex( COLOR_CYAN );
		re.SetColor( g_color_table[currentColorIndex] );
		i = (int)strlen( download.progress );
		dw = i * cls.smallchar_width;
		dx = (int)(( cls.glconfig.vidWidth - dw ) / 2);
		dlx1 = dx;
		dlx2 = dx + dw;
		for ( x = 0; x < i; x++ ) {
			SCR_DrawSmallChar( dx, lines, download.progress[x] );
			dx += cls.smallchar_width;
		}
		re.SetColor( NULL );
	}
#endif

	// draw breaker
	re.SetColor( foreColorValue );
	SCR_DrawPic( ax, ay + yf, wf, breakerWidth, cls.whiteShader );

	// draw clock
	if ( con_showClock->integer ) {
		qtime_t		t;
		const char *ts;
		int			tl;
		int			cx;

		Com_RealTime( &t );

		if ( con_showClock->integer > 1 ) {
			int h = t.tm_hour % 12;
			ts = va( "%d:%02d:%02d %s", h ? h : 12, t.tm_min, t.tm_sec, ( t.tm_hour >= 12 ) ? "PM" : "AM" );
		}
		else {
			ts = va( "%d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec );
		}
		tl = strlen( ts );
		cx = ax + cls.smallchar_width;

#ifdef USE_CURL
		if ( !download.progress[0] || ( download.progress[0] && dlx1 > cx + ( tl * cls.smallchar_width ) ) ) {
#endif
			SCR_DrawSmallString( cx, lines, ts, tl );
#ifdef USE_CURL
		}
#endif
	}

	// draw the version number
	if ( con_showVersion->integer ) {
		int vx = ax + wf - (int)( ARRAY_LEN( PRODUCT_VERSION ) * cls.smallchar_width );
#ifdef USE_CURL
		if ( !download.progress[0] || ( download.progress[0] && dlx2 < vx ) ) {
#endif
			SCR_DrawSmallString( vx, lines, PRODUCT_VERSION, ARRAY_LEN( PRODUCT_VERSION ) - 1 );
#ifdef USE_CURL
		}
#endif
	}

	re.SetColor( NULL );

	// draw the text
	con.visLines = lines;
	rows = lines / cls.smallchar_height - 1;	// rows of text to draw

	y = lines - ceil( (float)cls.smallchar_height * 2.25 );

	row = con.display;

	// draw from the bottom up
	if ( con.display != con.current ) {
		// draw arrows to show the buffer is backscrolled
		re.SetColor( foreColorValue );
		for ( x = 0; x < con.lineWidth; x += 4 )
			SCR_DrawSmallChar( ax + ( x + 1 ) * cls.smallchar_width, y, '^' );
		y -= cls.smallchar_height;
		row--;
	}
	
	currentColorIndex = ColorIndex( COLOR_WHITE );
	re.SetColor( g_color_table[currentColorIndex] );

	for ( i = 0; i < rows; i++, y -= cls.smallchar_height, row-- ) {
		if ( row < 0 )
			break;

		if ( con.current - row >= con.totalLines ) {
			// past scrollback wrap point
			continue;
		}

		text = con.text + ( row % con.totalLines ) * con.lineWidth;

		for ( x = 0; x < con.lineWidth; x++ ) {
			// skip rendering whitespace
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}
			// track color changes
			colorIndex = ( text[x] >> 8 ) & 63;
			if ( currentColorIndex != colorIndex ) {
				currentColorIndex = colorIndex;
				re.SetColor( g_color_table[colorIndex] );
			}
			SCR_DrawSmallChar( ax + ( x + 1 ) * cls.smallchar_width, y, text[x] & 0xff );
		}
	}

	// draw scrollbar
	if ( con_scrollBar->integer ) {
		const int height = endHeight - linesBottom;
		const int mx = ax + wf - cls.smallchar_width;
		const int my = ay + yf - height - linesBottom;
		const float tic = (float)height / (float)con.totalUsedLines;
		const float visHeight = tic * (float)rows;
		if ( con.totalUsedLines > rows ) {
			re.SetColor( colorBlack );
			SCR_DrawPic( mx, my, breakerWidth, height, cls.whiteShader );

			re.SetColor( foreColorValue );
			SCR_DrawPic( mx, my + height - visHeight - abs( tic * ( con.current - con.display ) ), breakerWidth, visHeight, cls.whiteShader );
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput( ax, lines - (int)ceil((float)cls.smallchar_height * 1.25) );

	re.SetColor( NULL );
}


/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	// check for console width changes from a vid mode change
	Con_CheckResize();

	// if disconnected, render console full screen
	//TODO: save buffer contents to draw under console when cl_loadScreenStyle = 0
	if ( cls.state == CA_DISCONNECTED || ( !cl_loadScreenStyle->integer && ( cls.state >= CA_CONNECTING && cls.state <= CA_PRIMED ) ) ) {
		if ( !( Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CGAME ) ) ) {
			Con_DrawSolidConsole( ( cls.state == CA_DISCONNECTED ) ? 1.0 : 0.5 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole( void ) {
	// decide on the destination height of the console
	if ( Key_GetCatcher() & KEYCATCH_CONSOLE )
		con.finalFrac = 0.5;	// half screen
	else
		con.finalFrac = 0.0;	// none visible

	// scroll towards the destination height
	if ( con.finalFrac < con.displayFrac ) {
		con.displayFrac -= con_speed->value * cls.realFrametime * 0.001;
		if ( con.finalFrac > con.displayFrac )
			con.displayFrac = con.finalFrac;

	} else if ( con.finalFrac > con.displayFrac ) {
		con.displayFrac += con_speed->value * cls.realFrametime * 0.001;
		if ( con.finalFrac < con.displayFrac )
			con.displayFrac = con.finalFrac;
	}
}


void Con_ScrollUp( int lines ) {
	if ( lines == 0 )
		lines = con.visPage - 2;

	con.display -= lines;

	Con_Fixup();
}


void Con_ScrollDown( int lines ) {
	if ( lines == 0 )
		lines = con.visPage - 2;

	con.display += lines;

	Con_Fixup();
}


void Con_Top( void ) {
	// this is generally incorrect but will be adjusted in Con_Fixup()
	con.display = con.current - con.totalLines;

	Con_Fixup();
}


void Con_Bottom( void ) {
	con.display = con.current;

	Con_Fixup();
}


void Con_Close( void ) {
	if ( !com_cl_running->integer )
		return;

	Field_Clear( &consoleField );
	Con_ClearNotify();
	Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_CONSOLE );
	con.finalFrac = 0.0;			// none visible
	con.displayFrac = 0.0;
}
