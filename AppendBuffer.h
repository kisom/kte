/*
 * AppendBuffer.h - selector header to choose GapBuffer or PieceTable
 */
#pragma once

#ifdef KTE_USE_PIECE_TABLE
#include "PieceTable.h"
using AppendBuffer = PieceTable;
#else
#include "GapBuffer.h"
using AppendBuffer = GapBuffer;
#endif