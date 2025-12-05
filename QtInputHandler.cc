// Refactor: route all key processing through command subsystem, mirroring ImGuiInputHandler

#include "QtInputHandler.h"

#include <QKeyEvent>

#include <ncurses.h>

#include "Editor.h"
#include "KKeymap.h"

// Temporary verbose logging to debug macOS Qt key translation issues
// Default to off; enable by defining QT_IH_DEBUG=1 at compile time when needed.
#ifndef QT_IH_DEBUG
#define QT_IH_DEBUG 0
#endif

#if QT_IH_DEBUG
#include <cstdio>
static const char *
mods_str(Qt::KeyboardModifiers m)
{
	static thread_local char buf[64];
	buf[0]     = '\0';
	bool first = true;
	auto add   = [&](const char *s) {
		if (!first)
			std::snprintf(buf + std::strlen(buf), sizeof(buf) - std::strlen(buf), "|");
		std::snprintf(buf + std::strlen(buf), sizeof(buf) - std::strlen(buf), "%s", s);
		first = false;
	};
	if (m & Qt::ShiftModifier)
		add("Shift");
	if (m & Qt::ControlModifier)
		add("Ctrl");
	if (m & Qt::AltModifier)
		add("Alt");
	if (m & Qt::MetaModifier)
		add("Meta");
	if (first)
		std::snprintf(buf, sizeof(buf), "none");
	return buf;
}
#define LOGF(...) std::fprintf(stderr, __VA_ARGS__)
#else
#define LOGF(...) ((void)0)
#endif

static bool
IsPrintableQt(const QKeyEvent &e)
{
	// Printable if it yields non-empty text and no Ctrl/Meta modifier
	if (e.modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
		return false;
	const QString t = e.text();
	return !t.isEmpty() && !t.at(0).isNull();
}


static int
ToAsciiKey(const QKeyEvent &e)
{
	const QString t = e.text();
	if (!t.isEmpty()) {
		const QChar c = t.at(0);
		if (!c.isNull())
			return KLowerAscii(c.unicode());
	}
	// When modifiers (like Control) are held, Qt::text() can be empty on macOS.
	// Fall back to mapping common virtual keys to ASCII.
	switch (e.key()) {
	case Qt::Key_A:
		return 'a';
	case Qt::Key_B:
		return 'b';
	case Qt::Key_C:
		return 'c';
	case Qt::Key_D:
		return 'd';
	case Qt::Key_E:
		return 'e';
	case Qt::Key_F:
		return 'f';
	case Qt::Key_G:
		return 'g';
	case Qt::Key_H:
		return 'h';
	case Qt::Key_I:
		return 'i';
	case Qt::Key_J:
		return 'j';
	case Qt::Key_K:
		return 'k';
	case Qt::Key_L:
		return 'l';
	case Qt::Key_M:
		return 'm';
	case Qt::Key_N:
		return 'n';
	case Qt::Key_O:
		return 'o';
	case Qt::Key_P:
		return 'p';
	case Qt::Key_Q:
		return 'q';
	case Qt::Key_R:
		return 'r';
	case Qt::Key_S:
		return 's';
	case Qt::Key_T:
		return 't';
	case Qt::Key_U:
		return 'u';
	case Qt::Key_V:
		return 'v';
	case Qt::Key_W:
		return 'w';
	case Qt::Key_X:
		return 'x';
	case Qt::Key_Y:
		return 'y';
	case Qt::Key_Z:
		return 'z';
	case Qt::Key_0:
		return '0';
	case Qt::Key_1:
		return '1';
	case Qt::Key_2:
		return '2';
	case Qt::Key_3:
		return '3';
	case Qt::Key_4:
		return '4';
	case Qt::Key_5:
		return '5';
	case Qt::Key_6:
		return '6';
	case Qt::Key_7:
		return '7';
	case Qt::Key_8:
		return '8';
	case Qt::Key_9:
		return '9';
	case Qt::Key_Comma:
		return ',';
	case Qt::Key_Period:
		return '.';
	case Qt::Key_Semicolon:
		return ';';
	case Qt::Key_Apostrophe:
		return '\'';
	case Qt::Key_Minus:
		return '-';
	case Qt::Key_Equal:
		return '=';
	case Qt::Key_Slash:
		return '/';
	case Qt::Key_Backslash:
		return '\\';
	case Qt::Key_BracketLeft:
		return '[';
	case Qt::Key_BracketRight:
		return ']';
	case Qt::Key_QuoteLeft:
		return '`';
	case Qt::Key_Space:
		return ' ';
	default:
		break;
	}
	return 0;
}


// Case-preserving ASCII derivation for k-prefix handling where we need to
// distinguish between 'C' and 'c'. Falls back to virtual-key mapping if
// event text is unavailable (common when Control/Meta held on macOS).
static int
ToAsciiKeyPreserveCase(const QKeyEvent &e)
{
	const QString t = e.text();
	if (!t.isEmpty()) {
		const QChar c = t.at(0);
		if (!c.isNull())
			return c.unicode();
	}
	// Fall back to virtual key mapping (letters as uppercase A..Z)
	switch (e.key()) {
	case Qt::Key_A:
		return 'A';
	case Qt::Key_B:
		return 'B';
	case Qt::Key_C:
		return 'C';
	case Qt::Key_D:
		return 'D';
	case Qt::Key_E:
		return 'E';
	case Qt::Key_F:
		return 'F';
	case Qt::Key_G:
		return 'G';
	case Qt::Key_H:
		return 'H';
	case Qt::Key_I:
		return 'I';
	case Qt::Key_J:
		return 'J';
	case Qt::Key_K:
		return 'K';
	case Qt::Key_L:
		return 'L';
	case Qt::Key_M:
		return 'M';
	case Qt::Key_N:
		return 'N';
	case Qt::Key_O:
		return 'O';
	case Qt::Key_P:
		return 'P';
	case Qt::Key_Q:
		return 'Q';
	case Qt::Key_R:
		return 'R';
	case Qt::Key_S:
		return 'S';
	case Qt::Key_T:
		return 'T';
	case Qt::Key_U:
		return 'U';
	case Qt::Key_V:
		return 'V';
	case Qt::Key_W:
		return 'W';
	case Qt::Key_X:
		return 'X';
	case Qt::Key_Y:
		return 'Y';
	case Qt::Key_Z:
		return 'Z';
	case Qt::Key_Comma:
		return ',';
	case Qt::Key_Period:
		return '.';
	case Qt::Key_Semicolon:
		return ';';
	case Qt::Key_Apostrophe:
		return '\'';
	case Qt::Key_Minus:
		return '-';
	case Qt::Key_Equal:
		return '=';
	case Qt::Key_Slash:
		return '/';
	case Qt::Key_Backslash:
		return '\\';
	case Qt::Key_BracketLeft:
		return '[';
	case Qt::Key_BracketRight:
		return ']';
	case Qt::Key_QuoteLeft:
		return '`';
	case Qt::Key_Space:
		return ' ';
	default:
		break;
	}
	return 0;
}


bool
QtInputHandler::ProcessKeyEvent(const QKeyEvent &e)
{
	const Qt::KeyboardModifiers mods = e.modifiers();
	LOGF("[QtIH] keyPress key=0x%X mods=%s text='%s' k_prefix=%d k_ctrl_pending=%d esc_meta=%d\n",
	     e.key(), mods_str(mods), e.text().toUtf8().constData(), (int)k_prefix_, (int)k_ctrl_pending_,
	     (int)esc_meta_);

	// Control-chord detection: only treat the physical Control key as control-like.
	// Do NOT include Meta (Command) here so that ⌘-letter shortcuts do not fall into
	// the Ctrl map (prevents ⌘-T being mistaken for C-t).
	const bool ctrl_like = (mods & Qt::ControlModifier);

	// 1) Universal argument digits (when active), consume digits without enqueuing commands
	if (ed_ &&ed_
	
	->
	UArg() != 0
	)
	{
		if (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
			if (e.key() >= Qt::Key_0 && e.key() <= Qt::Key_9) {
				int d = e.key() - Qt::Key_0;
				ed_->UArgDigit(d);
				// request status refresh
				std::lock_guard<std::mutex> lk(mu_);
				q_.push(MappedInput{true, CommandId::UArgStatus, std::string(), 0});
				LOGF("[QtIH] UArg digit %d -> enqueue UArgStatus\n", d);
				return true;
			}
		}
	}

	// 2) Enter k-prefix on C-k
	if (ctrl_like && (e.key() == Qt::Key_K)) {
		k_prefix_       = true;
		k_ctrl_pending_ = false;
		LOGF("[QtIH] Enter KPrefix\n");
		std::lock_guard<std::mutex> lk(mu_);
		q_.push(MappedInput{true, CommandId::KPrefix, std::string(), 0});
		return true;
	}

	// 3) If currently in k-prefix, resolve next key via KLookupKCommand
	if (k_prefix_) {
		// ESC/meta prefix should not interfere with k-suffix resolution
		esc_meta_ = false;
		// Support literal 'C' (uppercase) or '^' to indicate the next key is Ctrl-qualified.
		// Use case-preserving derivation so that 'c' (lowercase) can still be a valid suffix
		// like C-k c (BufferClose).
		int ascii_raw = ToAsciiKeyPreserveCase(e);
		if (ascii_raw == 'C' || ascii_raw == '^') {
			k_ctrl_pending_ = true;
			if (ed_)
				ed_->SetStatus("C-k C _");
			LOGF("[QtIH] KPrefix: set k_ctrl_pending via '%c'\n", (ascii_raw == 'C') ? 'C' : '^');
			return true; // consume, wait for next key
		}
		int ascii_key = (ascii_raw != 0) ? ascii_raw : ToAsciiKey(e);
		int lower     = KLowerAscii(ascii_key);
		// Only pass a control suffix for specific supported keys (d/x/q),
		// matching ImGui behavior so that holding Ctrl during the suffix
		// doesn't break other mappings like C-k c (BufferClose).
		bool ctrl_suffix_supported = (lower == 'd' || lower == 'x' || lower == 'q');
		bool pass_ctrl             = (ctrl_like || k_ctrl_pending_) && ctrl_suffix_supported;
		k_ctrl_pending_            = false; // consume pending qualifier on any suffix
		LOGF("[QtIH] KPrefix: ascii_key=%d lower=%d pass_ctrl=%d\n", ascii_key, lower, (int)pass_ctrl);
		if (ascii_key != 0) {
			CommandId id;
			if (KLookupKCommand(ascii_key, pass_ctrl, id)) {
				LOGF("[QtIH] KPrefix: mapped to command id=%d\n", (int)id);
				std::lock_guard<std::mutex> lk(mu_);
				q_.push(MappedInput{true, id, std::string(), 0});
			} else {
				// Unknown k-command: notify
				std::string a;
				a.push_back(static_cast<char>(ascii_key));
				LOGF("[QtIH] KPrefix: unknown command for '%c'\n", (char)ascii_key);
				std::lock_guard<std::mutex> lk(mu_);
				q_.push(MappedInput{true, CommandId::UnknownKCommand, a, 0});
			}
			k_prefix_ = false;
			return true;
		}
		// If not resolvable, consume and exit k-prefix
		k_prefix_ = false;
		LOGF("[QtIH] KPrefix: unresolved key; exiting prefix\n");
		return true;
	}

	// 3.5) GUI shortcut: Command/Meta + T opens the visual font picker (Qt only).
	// Require Meta present and Control NOT present so Ctrl-T never triggers this.
	if ((mods & Qt::MetaModifier) && !(mods & Qt::ControlModifier) && e.key() == Qt::Key_T) {
		LOGF("[QtIH] Meta/Super-T -> VisualFontPickerToggle\n");
		std::lock_guard<std::mutex> lk(mu_);
		q_.push(MappedInput{true, CommandId::VisualFontPickerToggle, std::string(), 0});
		return true;
	}

	// 4) ESC as Meta prefix (set state). Alt/Meta chord handled below directly.
	if (e.key() == Qt::Key_Escape) {
		esc_meta_ = true;
		LOGF("[QtIH] ESC: set esc_meta\n");
		return true; // consumed
	}

	// 5) Alt/Meta bindings (ESC f/b equivalent). Handle either Alt/Meta or pending esc_meta_
	// ESC/meta chords: on macOS, do NOT treat Meta as ESC; only Alt (Option) should trigger.
#if defined(__APPLE__)
	if (esc_meta_ || (mods & Qt::AltModifier)) {


#else
	if (esc_meta_ || (mods & (Qt::AltModifier | Qt::MetaModifier))) {
#endif
		int ascii_key = 0;
		if (e.key() == Qt::Key_Backspace) {
			ascii_key = KEY_BACKSPACE;
		} else if (e.key() >= Qt::Key_A && e.key() <= Qt::Key_Z) {
			ascii_key = 'a' + (e.key() - Qt::Key_A);
		} else if (e.key() == Qt::Key_Comma) {
			ascii_key = '<';
		} else if (e.key() == Qt::Key_Period) {
			ascii_key = '>';
		}
		// If still unknown, try deriving from text (covers digits, punctuation, locale)
		if (ascii_key == 0) {
			ascii_key = ToAsciiKey(e);
		}
		esc_meta_ = false; // one-shot regardless
		if (ascii_key != 0) {
			ascii_key = KLowerAscii(ascii_key);
			CommandId id;
			if (KLookupEscCommand(ascii_key, id)) {
				LOGF("[QtIH] ESC/Meta: mapped '%d' -> id=%d\n", ascii_key, (int)id);
				std::lock_guard<std::mutex> lk(mu_);
				q_.push(MappedInput{true, id, std::string(), 0});
				return true;
			} else {
				// Report invalid ESC sequence just like ImGui path
				LOGF("[QtIH] ESC/Meta: unknown command for ascii=%d\n", ascii_key);
				std::lock_guard<std::mutex> lk(mu_);
				q_.push(MappedInput{true, CommandId::UnknownEscCommand, std::string(), 0});
				return true;
			}
		}
		// Nothing derivable: consume (ESC prefix cleared) and do not insert text
		return true;
	}

	// 6) Control-chord direct mappings (e.g., C-n/C-p/C-f/C-b...)
	if (ctrl_like) {
		// Universal argument handling: C-u starts collection; C-g cancels
		if (e.key() == Qt::Key_U) {
			if (ed_)
				ed_->UArgStart();
			LOGF("[QtIH] Ctrl-chord: start universal argument\n");
			return true;
		}
		if (e.key() == Qt::Key_G) {
			if (ed_)
				ed_->UArgClear();
			k_ctrl_pending_ = false;
			k_prefix_       = false;
			LOGF("[QtIH] Ctrl-chord: cancel universal argument and k-prefix via C-g\n");
			// Fall through to map C-g to Refresh via ctrl map
		}
		if (e.key() >= Qt::Key_A && e.key() <= Qt::Key_Z) {
			int ascii_key = 'a' + (e.key() - Qt::Key_A);
			CommandId id;
			if (KLookupCtrlCommand(ascii_key, id)) {
				LOGF("[QtIH] Ctrl-chord: 'C-%c' -> id=%d\n", (char)ascii_key, (int)id);
				std::lock_guard<std::mutex> lk(mu_);
				q_.push(MappedInput{true, id, std::string(), 0});
				return true;
			}
		}
		// If no mapping, continue to allow other keys below
	}

	// 7) Special navigation/edit keys (match ImGui behavior)
	{
		CommandId id;
		bool has = false;
		switch (e.key()) {
		case Qt::Key_Return:
		case Qt::Key_Enter:
			id = CommandId::Newline;
			has = true;
			break;
		case Qt::Key_Backspace:
			id = CommandId::Backspace;
			has = true;
			break;
		case Qt::Key_Delete:
			id = CommandId::DeleteChar;
			has = true;
			break;
		case Qt::Key_Left:
			id = CommandId::MoveLeft;
			has = true;
			break;
		case Qt::Key_Right:
			id = CommandId::MoveRight;
			has = true;
			break;
		case Qt::Key_Up:
			id = CommandId::MoveUp;
			has = true;
			break;
		case Qt::Key_Down:
			id = CommandId::MoveDown;
			has = true;
			break;
		case Qt::Key_Home:
			id = CommandId::MoveHome;
			has = true;
			break;
		case Qt::Key_End:
			id = CommandId::MoveEnd;
			has = true;
			break;
		case Qt::Key_PageUp:
			id = CommandId::PageUp;
			has = true;
			break;
		case Qt::Key_PageDown:
			id = CommandId::PageDown;
			has = true;
			break;
		default:
			break;
		}
		if (has) {
			LOGF("[QtIH] Special key -> id=%d\n", (int)id);
			std::lock_guard<std::mutex> lk(mu_);
			q_.push(MappedInput{true, id, std::string(), 0});
			return true;
		}
	}

	// 8) Insert printable text
	if (IsPrintableQt(e)) {
		std::string s = e.text().toStdString();
		if (!s.empty()) {
			LOGF("[QtIH] InsertText '%s'\n", s.c_str());
			std::lock_guard<std::mutex> lk(mu_);
			q_.push(MappedInput{true, CommandId::InsertText, s, 0});
			return true;
		}
	}

	LOGF("[QtIH] Unhandled key\n");
	return false;
}


bool
QtInputHandler::Poll(MappedInput &out)
{
	std::lock_guard<std::mutex> lock(mu_);
	if (q_.empty())
		return false;
	out = q_.front();
	q_.pop();
	return true;
}