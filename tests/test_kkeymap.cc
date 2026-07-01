#include "Test.h"

#include "KKeymap.h"

#include <ncurses.h>


TEST(KKeymap_KPrefix_CanonicalChords)
{
	CommandId id{};

	// From docs/ke.md (K-commands)
	ASSERT_TRUE(KLookupKCommand('s', false, id));
	ASSERT_EQ(id, CommandId::Save);
	ASSERT_TRUE(KLookupKCommand('s', true, id)); // C-k C-s
	ASSERT_EQ(id, CommandId::Save);

	ASSERT_TRUE(KLookupKCommand('d', false, id));
	ASSERT_EQ(id, CommandId::KillToEOL);
	ASSERT_TRUE(KLookupKCommand('d', true, id)); // C-k C-d
	ASSERT_EQ(id, CommandId::KillLine);

	ASSERT_TRUE(KLookupKCommand(' ', false, id)); // C-k SPACE
	ASSERT_EQ(id, CommandId::ToggleMark);

	ASSERT_TRUE(KLookupKCommand('j', false, id));
	ASSERT_EQ(id, CommandId::JumpToMark);

	ASSERT_TRUE(KLookupKCommand('f', false, id));
	ASSERT_EQ(id, CommandId::FlushKillRing);

	ASSERT_TRUE(KLookupKCommand('y', false, id));
	ASSERT_EQ(id, CommandId::Yank);

	// Unknown should not map
	ASSERT_EQ(KLookupKCommand('Z', false, id), false);
}


TEST(KKeymap_KPrefix_UppercaseE_RejectedAndDistinctFromLowercaseE)
{
	CommandId id{};

	// 'e' is a real binding (OpenFileStart).
	ASSERT_TRUE(KLookupKCommand('e', false, id));
	ASSERT_EQ(id, CommandId::OpenFileStart);

	// 'E' must be rejected, not silently aliased to 'e' via case-insensitive
	// lookup (the switch normalizes to lowercase, so this has to be special-
	// cased before it).
	ASSERT_EQ(KLookupKCommand('E', false, id), false);
}


TEST(KKeymap_CtrlChords_CanonicalChords)
{
	CommandId id{};

	// From docs/ke.md (other keybindings)
	ASSERT_TRUE(KLookupCtrlCommand('n', id));
	ASSERT_EQ(id, CommandId::MoveDown);
	ASSERT_TRUE(KLookupCtrlCommand('p', id));
	ASSERT_EQ(id, CommandId::MoveUp);
	ASSERT_TRUE(KLookupCtrlCommand('f', id));
	ASSERT_EQ(id, CommandId::MoveRight);
	ASSERT_TRUE(KLookupCtrlCommand('b', id));
	ASSERT_EQ(id, CommandId::MoveLeft);

	ASSERT_TRUE(KLookupCtrlCommand('w', id));
	ASSERT_EQ(id, CommandId::KillRegion);
	ASSERT_TRUE(KLookupCtrlCommand('y', id));
	ASSERT_EQ(id, CommandId::Yank);

	ASSERT_EQ(KLookupCtrlCommand('z', id), false);
}


TEST(KKeymap_EscChords_CanonicalChords)
{
	CommandId id{};

	// From docs/ke.md (ESC bindings)
	ASSERT_TRUE(KLookupEscCommand('b', id));
	ASSERT_EQ(id, CommandId::WordPrev);
	ASSERT_TRUE(KLookupEscCommand('f', id));
	ASSERT_EQ(id, CommandId::WordNext);
	ASSERT_TRUE(KLookupEscCommand('d', id));
	ASSERT_EQ(id, CommandId::DeleteWordNext);
	ASSERT_TRUE(KLookupEscCommand('q', id));
	ASSERT_EQ(id, CommandId::ReflowParagraph);
	ASSERT_TRUE(KLookupEscCommand('w', id));
	ASSERT_EQ(id, CommandId::CopyRegion);

	// ESC BACKSPACE
	ASSERT_TRUE(KLookupEscCommand(KEY_BACKSPACE, id));
	ASSERT_EQ(id, CommandId::DeleteWordPrev);

	ASSERT_EQ(KLookupEscCommand('z', id), false);
}
