#include "TestRenderer.h"


void
TestRenderer::Draw(Editor &ed)
{
	(void) ed;
	++draw_count_;
}
