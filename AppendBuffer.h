/*
 * AppendBuffer.h - selector header to choose GapBuffer or PieceTable
 */
#ifndef KTE_APPENDBUFFER_H
#define KTE_APPENDBUFFER_H

#ifdef KTE_USE_PIECE_TABLE
#include "PieceTable.h"
using AppendBuffer = PieceTable;
#else
#include "GapBuffer.h"
using AppendBuffer = GapBuffer;
#endif

#endif // KTE_APPENDBUFFER_H
