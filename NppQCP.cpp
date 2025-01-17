// Quick Color Picker plugin for Notepad++

#include "NppQCP.h"

#include "QuickColorPicker\ColorPicker.h"
#include "QuickColorPicker\ColorPicker.res.h"

#include "csscolorparser.hpp"

#include <iostream>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <winver.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "version.lib")

// The data of Notepad++ that you can use in your plugin commands
// extern variables - don't change the name
NppData nppData;
FuncItem funcItem[COMMNAD_COUNT];
bool doCloseTag;


#define NPP_SUBCLASS_ID 101
#define MAX_COLOR_CODE_HIGHTLIGHT 80

#define TYPE_RGB 1
#define TYPE_RGBA 2
#define TYPE_HSL 3
#define TYPE_HSLA 4
#define TYPE_HEX 5

const wchar_t _ini_section[] = L"nppqcp";
const wchar_t _ini_key_enable[] = L"enabled";
const wchar_t _ini_key_highlight[] = L"highlight";
const wchar_t _ini_key_linethickness[] = L"linethickness";
const wchar_t _ini_key_hatchedhighlight[] = L"hatchedhighlight";
const wchar_t _ini_key_hatchedhighlight_type[] = L"hatchedhighlighttype";

const wchar_t _ini_file[] = L"nppqcp.ini";
TCHAR _ini_file_path[MAX_PATH];

struct ColorMarker {
	CSSColorParser::Color color;
	int start; 
	int end; 
	std::wstring HexString;

	ColorMarker()
	{
		color = CSSColorParser::Color();
		start = end = 0;
		HexString = L"000000";
	}

	LPCWSTR getLPCWSTR() const
	{
		return HexString.data();
	}
};

ColorMarker _color_markers[MAX_COLOR_CODE_HIGHTLIGHT];
int _color_marker_index = -1;

// color picker /////////////////////////////////////////////
QuickColorPicker::ColorPicker* _pColorPicker = NULL;
HINSTANCE _instance;
HWND _message_window;

bool _enable_qcp = false;
bool _enable_qcp_highlight = false;
bool _is_color_picker_shown = false;
bool _enable_hatchedhighlight = false;
int _linethickness = 4;
int _hatchedhighlight_type = 4;

int _qcp_cmd_index = 0;
int _highlight_cmd_index = 0;
int _hatch_cmd_index = 0;

int _current_type = 0;
int _replace_start = -1;
int _replace_end = -1;


////////////////////////////////////////
// Plugin Init & Clean up
////////////////////////////////////////
void AttachDll(HANDLE module) {

	_instance = (HINSTANCE)module;

}

// Initialize plugin - will be called when setInfo(), after nppData is available
void PluginInit() {

	LoadConfig();

	CreateMessageWindow();
	AddNppSubclass();

	InitCommandMenu();

}

// End plugin
void PluginCleanUp() {

	SaveConfig();

	DestroyMessageWindow();
	RemoveNppSubclass();

}



////////////////////////////////////////
// Access config file
////////////////////////////////////////
void LoadConfig(){

	// get path of plugin configuration
	::SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)_ini_file_path);

	if (PathFileExists(_ini_file_path) == FALSE) {
		::CreateDirectory(_ini_file_path, NULL);
	}

	PathAppend(_ini_file_path, _ini_file);

	// read in config
	int enabled = ::GetPrivateProfileInt(_ini_section, _ini_key_enable, 5, _ini_file_path);
	if(enabled == 5)
		enabled = 1;
	_enable_qcp = ( enabled == 1);

	enabled = ::GetPrivateProfileInt(_ini_section, _ini_key_highlight, 5, _ini_file_path);
	if(enabled == 5)
		enabled = 1;
	_enable_qcp_highlight = (enabled == 1);

	enabled = ::GetPrivateProfileInt(_ini_section, _ini_key_hatchedhighlight, 5, _ini_file_path);
	if (enabled == 5)
		enabled = 1;
	_enable_hatchedhighlight = (enabled == 1);
	
	_linethickness = ::GetPrivateProfileInt(_ini_section, _ini_key_linethickness, 4, _ini_file_path);

	_hatchedhighlight_type = ::GetPrivateProfileInt(_ini_section, _ini_key_hatchedhighlight_type, 0, _ini_file_path);

}

void SaveConfig(){

	::WritePrivateProfileString(
		_ini_section, _ini_key_enable,
		_enable_qcp ? L"1" : L"0",
		_ini_file_path
	);

	::WritePrivateProfileString(
		_ini_section, _ini_key_highlight,
		_enable_qcp_highlight ? L"1" : L"0",
		_ini_file_path
	);

	
	::WritePrivateProfileString(
		_ini_section, _ini_key_linethickness,
		std::to_wstring(_linethickness).c_str(),
		_ini_file_path
	);


	::WritePrivateProfileString(
		_ini_section, _ini_key_hatchedhighlight_type,
		std::to_wstring(_hatchedhighlight_type).c_str(),
		_ini_file_path
	);


	::WritePrivateProfileString(
		_ini_section, _ini_key_hatchedhighlight,
		_enable_hatchedhighlight ? L"1" : L"0",
		_ini_file_path
	);

}


////////////////////////////////////////
// Menu Command
////////////////////////////////////////
void InitCommandMenu() {

    // setCommand(int index,                      // zero based number to indicate the order of command
    //            wchar_t *commandName,             // the command name that you want to see in plugin menu
    //            PFUNCPLUGINCMD functionPointer, // the symbol of function (function pointer) associated with this command. The body should be defined below. See Step 4.
    //            ShortcutKey *shortcut,          // optional. Define a shortcut to trigger this command
    //            bool checkOnInit                // optional. Make this menu item be checked visually
    //            );

	int index = 0;

	setCommand(index++, L"+ Open Color Palette", InsertByPalette, NULL, false);
	setCommand(index++, L"+ Pick Color from Screen", PickFromScreen, NULL, false);
	setCommand(index++, L"---", NULL, NULL, false);
	_qcp_cmd_index = index++;
	setCommand(_qcp_cmd_index, L"Enable Quick Color Picker", ToggleQCP, NULL, _enable_qcp);
	_highlight_cmd_index = index++;
	setCommand(_highlight_cmd_index, L"Underline Hex Codes", ToggleColorHighlight, NULL, _enable_qcp_highlight);
	_hatch_cmd_index = index++;
	setCommand(_hatch_cmd_index, L"Hatched Highlight on Hex Codes", ToggleHatchHighlight, NULL, _enable_hatchedhighlight);
	setCommand(index++, L"---", NULL, NULL, false);

	// get version
	WCHAR fileName[_MAX_PATH];
	DWORD size = GetModuleFileName(_instance, fileName, _MAX_PATH);
	fileName[size] = NULL;
	DWORD handle = 0;
	size = GetFileVersionInfoSize(fileName, &handle);
	BYTE* versionInfo = new BYTE[size];
	wchar_t version[50];

	if (!GetFileVersionInfo(fileName, handle, size, versionInfo)) {
		delete[] versionInfo;
		lstrcpy(version, L"Unknown");
	}

	UINT    			len = 0;
	VS_FIXEDFILEINFO*   vsfi = NULL;
	VerQueryValue(versionInfo, L"\\", (void**)&vsfi, &len);
	wsprintf(version, L"%d.%d.%d",
		HIWORD(vsfi->dwFileVersionMS),
		LOWORD(vsfi->dwFileVersionMS),
		HIWORD(vsfi->dwFileVersionLS)
	);
	delete[] versionInfo;

	// create visit website link
	wchar_t text[200] = L"Visit NPPQCP Website ";
	wcscat_s(text, L" (Version ");
	wcscat_s(text, version);
	wcscat_s(text, L")");

	setCommand(index++, text, VisitWebsite, NULL, FALSE);

}

bool setCommand(size_t index, wchar_t *cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey *sk, bool checkOnInit) {

    if (index >= COMMNAD_COUNT)
        return false;

    if (!pFunc)
        return false;

    wcscpy_s(funcItem[index]._itemName, cmdName);
    funcItem[index]._pFunc = pFunc;
    funcItem[index]._init2Check = checkOnInit;
    funcItem[index]._pShKey = sk;

    return true;
}

void commandMenuCleanUp() {

}


///////////////////////////////////////////////////////////
// MENU COMMANDS
///////////////////////////////////////////////////////////

// Open QCP directly
void InvokeColorPicker(bool use_screen_picker) {

	// create the color picker if not created
	if (!_pColorPicker)
		CreateColorPicker();

	HWND h_scintilla = GetScintilla();
	_replace_start = (int)::SendMessage(h_scintilla, SCI_GETSELECTIONSTART, 0, 0);
	_replace_end = (int)::SendMessage(h_scintilla, SCI_GETSELECTIONEND, 0, 0);
	_current_type = TYPE_HEX;

	if (use_screen_picker) {
		_pColorPicker->ShowScreenPicker();
	}
	else {
		PlaceColorPickerAt(h_scintilla, _replace_start);
		_pColorPicker->Show();
	}

}

// Pick a Color from Palette
void InsertByPalette(){
	InvokeColorPicker();
}

// Pick a Color from screen
void PickFromScreen() {
	InvokeColorPicker(true);
}


// toggle QCP plugin
void ToggleQCP() {

	_enable_qcp = !_enable_qcp;
	
	if (_enable_qcp) {
		HighlightColorCode();
	} else {
		ClearColorMarkers();
		HideColorPicker();
	}

	::CheckMenuItem(::GetMenu(nppData._nppHandle), funcItem[_qcp_cmd_index]._cmdID, MF_BYCOMMAND | (_enable_qcp ? MF_CHECKED : MF_UNCHECKED));

}


// toggle color highlight
void ToggleHatchHighlight() {

	_enable_hatchedhighlight = !_enable_hatchedhighlight;

	::CheckMenuItem(::GetMenu(nppData._nppHandle), funcItem[_hatch_cmd_index]._cmdID, MF_BYCOMMAND | (_enable_hatchedhighlight ? MF_CHECKED : MF_UNCHECKED));

	if (_enable_hatchedhighlight) {
		HighlightColorCode();
	}
	else {
		ClearColorMarkers();
	}
}

void ToggleColorHighlight() {

	_enable_qcp_highlight = !_enable_qcp_highlight;

	::CheckMenuItem(::GetMenu(nppData._nppHandle), funcItem[_highlight_cmd_index]._cmdID, MF_BYCOMMAND | (_enable_qcp_highlight ? MF_CHECKED : MF_UNCHECKED));

	if (_enable_qcp_highlight) {
		HighlightColorCode();
	} else {
		ClearColorMarkers();
	}

}


// visit nppqcp website
void VisitWebsite() {

	wchar_t url[200] = L"https://github.com/nulled666/nppqcp/";
	::ShellExecute(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);

}


///////////////////////////////////////////////////////////
//  MESSAGE WINDOW
///////////////////////////////////////////////////////////

// create a background window for message communication
void CreateMessageWindow() {
	
	static wchar_t szWindowClass[] = L"npp_qcp_msgwin";
	
	WNDCLASSEX wc    = {0};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = MessageWindowWinproc;
	wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = _instance;
	wc.lpszClassName = szWindowClass;

	if (!RegisterClassEx(&wc)) {
		throw std::runtime_error("NppQCP: RegisterClassEx() failed");
	}

	_message_window = CreateWindowEx(0, szWindowClass, szWindowClass, WS_POPUP,	0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);

	if (!_message_window) {
		throw std::runtime_error("NppQCP: CreateWindowEx() function returns null");
	}

}

void DestroyMessageWindow() {

	::DestroyWindow(_message_window);
	::UnregisterClass(L"npp_qcp_msgwin", _instance);

}

// message proccessor
LRESULT CALLBACK MessageWindowWinproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {

	switch (message) {
		case WM_QCP_PICK:
		{
			::SetActiveWindow(nppData._nppHandle);
			WriteColor((COLORREF)wparam);
			break;
		}
		case WM_QCP_CANCEL:
		{
			::SetActiveWindow(nppData._nppHandle);
			break;
		}
		case WM_QCP_START_SCREEN_PICKER:
		{
			::SetWindowPos(nppData._nppHandle, HWND_BOTTOM, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
		}
		case WM_QCP_END_SCREEN_PICKER:
		{
			::ShowWindow(nppData._nppHandle, SW_SHOW);
		}
		default:
		{
			return TRUE;
		}
	}

	return TRUE;

}


// Get current scintilla comtrol hwnd
HWND GetScintilla() {

    int which = -1;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    if (which == -1)
        return NULL;

    HWND h_scintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
	
	return h_scintilla;

}

////////////////////////////////////////////////////////////////////////////////
// NPP SUBCLASSING
////////////////////////////////////////////////////////////////////////////////

void AddNppSubclass(){

	::SetWindowSubclass(nppData._scintillaMainHandle, NppSubclassProc, NPP_SUBCLASS_ID, NULL);
	::SetWindowSubclass(nppData._scintillaSecondHandle, NppSubclassProc, NPP_SUBCLASS_ID, NULL);

}

void RemoveNppSubclass(){
	
	::RemoveWindowSubclass(nppData._scintillaMainHandle, NppSubclassProc, NPP_SUBCLASS_ID);
	::RemoveWindowSubclass(nppData._scintillaSecondHandle, NppSubclassProc, NPP_SUBCLASS_ID);

}

LRESULT CALLBACK NppSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
	
	switch (message) {
		case WM_KEYDOWN:
			// hide the palette when user is typing or copy/paste
			HideColorPicker();
			break;
	}

	return DefSubclassProc(hwnd, message, wparam, lparam);

}


////////////////////////////////////////////////////////////////////////////////
// COLOR PALETTE POPUP
////////////////////////////////////////////////////////////////////////////////

void CreateColorPicker(){

	_pColorPicker = new QuickColorPicker::ColorPicker();
	_pColorPicker->focus_on_show = false;
	_pColorPicker->Create(_instance, nppData._nppHandle, _message_window);

	LoadRecentColor();

	::SetActiveWindow(nppData._nppHandle);

}

bool ShowColorPicker(){

	if (!_enable_qcp)
		return false;

	HWND h_scintilla = GetScintilla();

	// check for selection
	int selection_start = (int)::SendMessage(h_scintilla, SCI_GETSELECTIONSTART, 0, 0);
	int selection_end = (int)::SendMessage(h_scintilla, SCI_GETSELECTIONEND, 0, 0);

	// nothing selected
	if(selection_start==selection_end)
		return false;

	// create the color picker if not created
	if (!_pColorPicker)
		CreateColorPicker();

	// check for color codes
	_current_type = 0;

	bool result = CheckSelectionForHexColor(h_scintilla, selection_start, selection_end);
	if (!result) {
		result = CheckSelectionForBracketColor(h_scintilla, selection_start, selection_end);
	}

	if(!result)
		return false;

	// set and show
	PlaceColorPickerAt(h_scintilla, selection_start);
	_pColorPicker->Show();

	return true;

}

void PlaceColorPickerAt(HWND h_scintilla, int pos) {

	// prepare coordinates
	POINT p;
	p.x = (int)::SendMessage(h_scintilla, SCI_POINTXFROMPOSITION, 0, pos);
	p.y = (int)::SendMessage(h_scintilla, SCI_POINTYFROMPOSITION, 0, pos);
	::ClientToScreen(h_scintilla, &p);

	// all line height in scintilla is the same
	int line_height = (int)::SendMessage(h_scintilla, SCI_TEXTHEIGHT, 0, 1);

	RECT rc;
	rc.top = p.y;
	rc.right = p.x; // not used anyway
	rc.bottom = p.y + line_height;
	rc.left = p.x;

	_pColorPicker->SetParentRect(rc);

}

bool CheckSelectionForHexColor(const HWND h_scintilla, const int start, const int end){
	
	int len = end - start;

	// fail - wrong length: fc6 ffcc66
	if (len != 3 && len != 6)
		return false;

	char prev_char = (char)::SendMessage(h_scintilla, SCI_GETCHARAT, start - 1, 0);
	// fail - no # mark
	if (prev_char == 0 || prev_char != '#')
		return false;

	char next_char = (char)::SendMessage(h_scintilla, SCI_GETCHARAT, end, 0);

	// fail - next char is still hex char
	if (next_char != 0 && strchr("01234567890abcdefABCDEF", next_char) != NULL)
		return false;

	// passed -
	_current_type = TYPE_HEX;

	// save selection range
	_replace_start = start - 1;
	_replace_end = end;

	// get hex text - scintilla only accept char
	char hex_str[10];
    ::SendMessage(h_scintilla, SCI_GETSELTEXT, 0, (LPARAM)&hex_str);

	// put current color to picker
	if (!_pColorPicker->SetHexColor(hex_str)) {
		// not a valid hex color string
		return false;
	}

	return true;

}

bool CheckSelectionForBracketColor(const HWND h_scintilla, const int start, const int end){
	
	int len = end - start;

	// fail - wrong length (rgb rgba hsl hsla)
	if (len != 3 && len != 4)
		return false;

	char next_char = (char)::SendMessage(h_scintilla, SCI_GETCHARAT, end, 0);

	// fail - next char should be the open bracket
	if (next_char != '(')
		return false;

	// check for prefix
	char prefix[8];
	::SendMessage(h_scintilla, SCI_GETSELTEXT, 0, (LPARAM)&prefix);

	if (strcmp(prefix,"rgb") == 0) {
		_current_type = TYPE_RGB;
	}
	else if (strcmp(prefix,"rgba") == 0) {
		_current_type = TYPE_RGBA;
	}
	else if (strcmp(prefix, "hsl") == 0) {
		_current_type = TYPE_HSL;
	}
	else if (strcmp(prefix, "hsla") == 0) {
		_current_type = TYPE_HSLA;
	}
	else {
		return false;
	}

	// read in the whole string and parse
	char buff[50];

	int line = (int)::SendMessage(h_scintilla, SCI_LINEFROMPOSITION, (WPARAM)start, 0);
	int line_end = (int)::SendMessage(h_scintilla, SCI_GETLINEENDPOSITION, (WPARAM)line, 0);
	int range_end = end + 45;
	range_end = range_end > line_end ? line_end : range_end;

	Sci_TextRangeFull tr;
	tr.chrg.cpMin = start;
	tr.chrg.cpMax = range_end;
	tr.lpstrText = buff;

	::SendMessage(h_scintilla, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&tr);

	// check for validity 
	int close_pos = start;
	int buff_len = sizeof(buff);

	for (int i = (int)strlen(prefix) + 1; i < buff_len; i++) {
		char ch = buff[i];
		if (ch == '\0') {
			// no close bracket ')' found
			return false;
		}
		else if ( ch == ')' ) {
			if (i < 6) {
				// too short
				return false;
			}
			else {
				// close bracket ')' found, cut the end and continue
				buff[i + 1] = '\0';
				close_pos += i;
				break;
			}
		}
		else if ( strchr("01234567890 ,.%", ch) == NULL ) {
			// contains invalid char
			return false;
		}
	}

	CSSColorParser::Color color = CSSColorParser::parse(buff);
	QuickColorPicker::RGBAColor rgb = QuickColorPicker::RGBAColor(color.r, color.g, color.b, color.a);

	// prepare seletion range for replacement
	_replace_start = start;
	_replace_end = close_pos + 1;

	// put current color to picker
	_pColorPicker->SetColor(rgb);

	return true;

}

// hide the palette
void HideColorPicker() {
	
	if (!_pColorPicker)
		return;

	if (!_pColorPicker->IsVisible())
		return;

	_pColorPicker->Hide();

}


bool HasSelection(){

	HWND h_scintilla = GetScintilla();
	int selection_start = (int)::SendMessage(h_scintilla, SCI_GETSELECTIONSTART, 0, 0);
	int selection_end = (int)::SendMessage(h_scintilla, SCI_GETSELECTIONEND, 0, 0);

	if(selection_start==selection_end)
		return false;

	return true;

}


void WriteColor(COLORREF color) {

	HWND h_scintilla = GetScintilla();

	QuickColorPicker::RGBAColor rgb = _pColorPicker->GetColor();

	// change type if color has alpha channel
	if (rgb.a < 1.0f) {
		if (_current_type == TYPE_HEX) {
			_current_type = TYPE_RGBA;
		}
		else if (_current_type == TYPE_RGB) {
			_current_type = TYPE_RGBA;
		}
		else if (_current_type == TYPE_HSL) {
			_current_type = TYPE_HSLA;
		}
	}else{
		if (_current_type == TYPE_RGBA) {
			_current_type = TYPE_HEX;
		}
		else if (_current_type == TYPE_HSLA){
			_current_type = TYPE_HSL;
		}
	}

	char buff[100];

	if(_current_type == TYPE_HEX){

		// hex string
		char hex[8];
		_pColorPicker->GetHexColor(hex, sizeof(hex));
		sprintf_s(buff, "#%hs", hex);

	}else{
		// bracket color
		if (_current_type == TYPE_RGB) {
			sprintf_s(buff, "rgb(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
		}
		else if (_current_type == TYPE_RGBA){
			sprintf_s(buff, "rgba(%d,%d,%d,%.2g)", rgb.r, rgb.g, rgb.b, rgb.a);
		}
		else if (_current_type == TYPE_HSL || _current_type == TYPE_HSLA) {
			QuickColorPicker::HSLAColor hsl = _pColorPicker->GetHSLAColor();
			int h = (int)round(hsl.h);
			int s = (int)round(hsl.s * 100);
			int l = (int)round(hsl.l * 100);
			if (_current_type == TYPE_HSL) {
				sprintf_s(buff, "hsl(%d,%d%%,%d%%)", h, s, l);
			}
			else {
				sprintf_s(buff, "hsla(%d,%d%%,%d%%,%.2g)", h, s, l, hsl.a);
			}
		}

	}
		
	// replace range
	::SendMessage(h_scintilla, SCI_SETSELECTIONSTART, _replace_start, 0);
	::SendMessage(h_scintilla, SCI_SETSELECTIONEND, _replace_end, 0);
	::SendMessage(h_scintilla, SCI_REPLACESEL, NULL, (LPARAM)(char*)buff);

	::SetActiveWindow(h_scintilla);

	SaveRecentColor();

}

// load & save recent used colors
void LoadRecentColor(){

	if (!_pColorPicker)
		return;

	QuickColorPicker::RGBAColor colors[16];
	COLORREF color;
	wchar_t key[20];
	wchar_t alpha[20];

	for (int i=0; i<16; i++) {
		swprintf_s(key, L"recent%d", i);
		color = ::GetPrivateProfileInt(_ini_section, key, 0, _ini_file_path);		
		colors[i] = QuickColorPicker::RGBAColor(color);
		swprintf_s(key, L"recent%d_a", i);
		color = ::GetPrivateProfileString(_ini_section, key, L"1", alpha, sizeof(alpha), _ini_file_path);
		if (color > 0) {
			colors[i].a = wcstof(alpha, nullptr);
		}
	}

	_pColorPicker->SetRecentColor(colors);

}

void SaveRecentColor(){
	
	if(!_pColorPicker)
		return;

	QuickColorPicker::RGBAColor colors[16];
	QuickColorPicker::RGBAColor* p_colors = colors;
	_pColorPicker->GetRecentColor(p_colors);

	wchar_t color[20];
	wchar_t key[20];

	for (int i=0; i<16; i++) {
		swprintf_s(color, L"%d", (COLORREF)colors[i]);
		swprintf_s(key, L"recent%d", i);
		::WritePrivateProfileString(_ini_section, key, color, _ini_file_path);
		swprintf_s(color, L"%.2g", colors[i].a);
		swprintf_s(key, L"recent%d_a", i);
		::WritePrivateProfileString(_ini_section, key, color, _ini_file_path);
	}

}



////////////////////////////////////////////////////////////////////////////////
// highlight hex color
////////////////////////////////////////////////////////////////////////////////
void HighlightColorCode() {
	
	const bool bAnyAvailableHighlights = _enable_hatchedhighlight || _enable_qcp_highlight;
	
	if (!_enable_qcp || !bAnyAvailableHighlights)
	{
		return;
	}

	int lang = -100;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTLANGTYPE, 0, (LPARAM)&lang);

	if (lang != L_CSS && lang != L_JS && lang != L_HTML) {
        return;
    }

    HWND h_scintilla = GetScintilla();

	// determine parse region

    //SCI_GETFIRSTVISIBLELINE first line is 0
    int first_visible_line = (int)::SendMessage(h_scintilla, SCI_GETFIRSTVISIBLELINE, 0, 0);
	int last_line = first_visible_line + (int)::SendMessage(h_scintilla, SCI_LINESONSCREEN, 0, 0);

	first_visible_line = first_visible_line - 1; // i don't know why - but this fix the missing color

    int start_position = 0;
	if(first_visible_line>1)
		start_position = (int)::SendMessage(h_scintilla, SCI_POSITIONFROMLINE, first_visible_line, 0);

	int end_position = (int)::SendMessage(h_scintilla, SCI_GETLINEENDPOSITION, last_line, 0);

	// generate marker list
	FindHexColor(h_scintilla, start_position, end_position);

	char *suff[4] = { "rgb(", "rgba(", "hsl(", "hsla(" };
	for (int i = 0; i < 4; i++) {
		FindBracketColor(h_scintilla, start_position, end_position, suff[i]);
	}

	// draw the marker list
	DrawColorMarkers(h_scintilla);

}


void FindHexColor(const HWND h_scintilla, const int start_position, const int end_position){

	bool marker_not_full = true;
	int search_start = start_position;

    while (marker_not_full && search_start < end_position) {

		Sci_TextToFindFull tf;
		tf.chrg.cpMin = search_start;
		tf.chrg.cpMax = end_position+1;
		tf.lpstrText = "#";

		const Sci_TextToFindFull* tf_Ptr = &tf;
		int target_pos = (int)::SendMessage(h_scintilla, SCI_FINDTEXTFULL, 0, (LPARAM)&tf);

		// not found
		if(target_pos == -1) {
			break;
		}

		// read in the possible color code sequence
		int line = (int)::SendMessage(h_scintilla, SCI_LINEFROMPOSITION, (WPARAM)target_pos, 0);
		int line_end = (int)::SendMessage(h_scintilla, SCI_GETLINEENDPOSITION, (WPARAM)line, 0);
		int range_end = target_pos + 9;
		range_end = range_end > line_end ? line_end : range_end;

		char buff[10];
		int buff_length = sizeof(buff);

		Sci_TextRangeFull tr;
		tr.chrg.cpMin = target_pos;
		tr.chrg.cpMax = range_end;
		tr.lpstrText = buff;

		::SendMessage(h_scintilla, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&tr);
		
		bool is_valid = false;

		for (int i = 1; i < 10; i++) {
			char ch = buff[i];
			// Comma included
			if (strchr(" ;,.)\0\n", ch) != NULL) {
				// delimiter found
				if (i == 4 || i == 7) {
					// is the right length
					buff[i] = '\0';
					is_valid = true;
				}
				break;
			}
			else if (strchr("0123456789abcdefABCDEF", ch) == NULL){
				// invalid char found
				break;
			}
		}

		// invalid - continue
		if (!is_valid) {
			search_start = target_pos + 1; // move on
			continue;
		}

		// align the positions
        int target_length = (int)strlen(buff);
        int target_start = target_pos;
        int target_end = target_pos + target_length;

		// parse color string
		CSSColorParser::Color css_color = CSSColorParser::parse(buff);

		marker_not_full = SaveColorMarker(css_color, target_start, target_end, buff);

		search_start = target_end; // move on
    }

}


void FindBracketColor(const HWND h_scintilla, const int start_position, const int end_position, char* prefix) {

	int prefix_len = (int)strlen(prefix) - 1;

	bool marker_not_full = true;
	int search_start = start_position;
	int search_end = end_position + 1;

	while (marker_not_full && search_start < end_position) {

		Sci_TextToFindFull tf;
		tf.chrg.cpMin = search_start;
		tf.chrg.cpMax = search_end;
		tf.lpstrText = prefix;

		int target_pos = (int)::SendMessage(h_scintilla, SCI_FINDTEXTFULL, 0, (LPARAM)&tf);

		// not found
		if (target_pos == -1) {
			break;
		}

		// read in the possible color code sequence
		int line = (int)::SendMessage(h_scintilla, SCI_LINEFROMPOSITION, (WPARAM)target_pos, 0);
		int line_end = (int)::SendMessage(h_scintilla, SCI_GETLINEENDPOSITION, (WPARAM)line, 0);
		int range_end = target_pos + 45;
		range_end = range_end > line_end ? line_end : range_end;

		char buff[50];
		int buff_length = sizeof(buff);

		Sci_TextRangeFull tr;
		tr.chrg.cpMin = target_pos;
		tr.chrg.cpMax = range_end;
		tr.lpstrText = buff;

		::SendMessage(h_scintilla, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&tr);

		bool is_valid = false;

		for (int i = (int)strlen(prefix) + 1; i < buff_length; i++) {
			char ch = buff[i];
			if (ch == '\0') {
				// no close bracket ')' found
				break;
			}
			else if (ch == ')') {
				if (i < 6) {
					// too short
					break;
				}
				else {
					// close bracket ')' found, cut the end and continue
					buff[i + 1] = '\0';
					is_valid = true;
					break;
				}
			}
			else if (strchr("01234567890 ,.%", ch) == NULL) {
				// contains invalid char
				break;
			}
		}

		// invalid - continue
		if (!is_valid) {
			search_start = target_pos + prefix_len; // move on
			continue;
		}

		// align the positions
		int target_length = (int)strlen(buff);
		int target_start = target_pos;
		int target_end = target_pos + target_length;


		// parse color string
		CSSColorParser::Color css_color = CSSColorParser::parse(buff);

		marker_not_full = SaveColorMarker(css_color, target_start, target_end, buff);

		search_start = target_end; // move on

	}

}

bool SaveColorMarker(CSSColorParser::Color color, int marker_start, int marker_end, const std::string& String) {

	_color_marker_index++;

	if (_color_marker_index < MAX_COLOR_CODE_HIGHTLIGHT) {


		ColorMarker& ThisColourMarker = _color_markers[_color_marker_index];
		std::wstring& HexString = ThisColourMarker.HexString;
		HexString.resize(String.length());
		for (int i = 0; i < String.length(); i++)
		{
			HexString[i] = String[i];
		}
		
		ThisColourMarker.color = color;
		ThisColourMarker.start = marker_start;
		ThisColourMarker.end = marker_end;

		return true;
	}
	else {
		return false;
	}

}

void EmptyColorMarker() {

	_color_marker_index = -1;

}

//https://stackoverflow.com/questions/3942878/how-to-decide-font-color-in-white-or-black-depending-on-background-color
COLORREF _ChooseBackgroundTextColour(const COLORREF Background)
{
	int r, g, b;
	r = GetRValue(Background);
	g = GetGValue(Background);
	b = GetBValue(Background);
	const float Intensity = (r * 0.299f) + (g * 0.587f) + (b * 0.114f);

	if (Intensity > 150)
	{
		return RGB(0, 0, 0);
	}
	else
	{
		return RGB(255, 255, 255);
	}
}

void DrawColorMarkers(const HWND h_scintilla) {
	
	if (_color_marker_index < 0)
	{
		return;
	}

	HDC hdc_editor = ::GetDC(h_scintilla);
	RECT HatchedRect = RECT();
	RECT UnderlineRect = RECT();

	RECT TextRect = RECT();
	LPRECT TextRectPtr = LPRECT();
	TextRectPtr = &TextRect;
	
	HBRUSH brush;

	int list_length = _color_marker_index + 1;
	
	::SetBkMode(hdc_editor, TRANSPARENT);

	for (int i = 0; i < list_length; i++) {

		const ColorMarker& ThisColourMarker = _color_markers[i];

		//const int length = cm.end - cm.start;

		const int start_x = (int)::SendMessage(h_scintilla, SCI_POINTXFROMPOSITION, 0, ThisColourMarker.start);
		const int start_y = (int)::SendMessage(h_scintilla, SCI_POINTYFROMPOSITION, 0, ThisColourMarker.start);
		const int end_x = (int)::SendMessage(h_scintilla, SCI_POINTXFROMPOSITION, 0, ThisColourMarker.end);
		const int end_y = (int)::SendMessage(h_scintilla, SCI_POINTYFROMPOSITION, 0, ThisColourMarker.end);

		const int line_height = (int)::SendMessage(h_scintilla, SCI_TEXTHEIGHT, 0, 0);

		// convert to COLORREF
		COLORREF colorref = RGB(ThisColourMarker.color.r, ThisColourMarker.color.g, ThisColourMarker.color.b);

		// paint swatch /////////
		
		// new color
		if (_enable_hatchedhighlight)
		{
			HatchedRect.left = start_x;
			HatchedRect.right = end_x;
			HatchedRect.top = start_y + line_height;
			HatchedRect.bottom = start_y;
			if (_hatchedhighlight_type < 0)
			{
				brush = ::CreateSolidBrush(colorref);
			}
			else
			{
				brush = ::CreateHatchBrush(_hatchedhighlight_type, colorref);
			}

			::FillRect(hdc_editor, &HatchedRect, brush);
			::DeleteObject(brush);

			TextRectPtr->left = start_x;
			TextRectPtr->right = end_x;
			TextRectPtr->top = start_y;
			TextRectPtr->bottom = start_y;
			

			const COLORREF TextColor = _ChooseBackgroundTextColour(colorref);
			
			::SetTextAlign(hdc_editor, TA_LEFT);
			::SetTextColor(hdc_editor, TextColor);
			::DrawText(
				hdc_editor,
				ThisColourMarker.getLPCWSTR(), 
				6,
				TextRectPtr, 
				DT_SINGLELINE | DT_NOCLIP
			);
		}
		if (_enable_qcp_highlight)
		{
			UnderlineRect.left = start_x;
			UnderlineRect.right = end_x;
			UnderlineRect.top = start_y + line_height - (_linethickness / 2);
			UnderlineRect.bottom = start_y + line_height + (_linethickness / 2);
			brush = ::CreateSolidBrush(colorref);
			::FillRect(hdc_editor, &UnderlineRect, brush);
			::DeleteObject(brush);
		}
	
	}


	::ReleaseDC(h_scintilla, hdc_editor);

	EmptyColorMarker();
	::SetBkMode(hdc_editor, OPAQUE);

}


void ClearColorMarkers() {

	EmptyColorMarker();

	//HWND h_scintilla = GetScintilla();
	//::RedrawWindow(h_scintilla, NULL, NULL, RDW_INVALIDATE);

}
