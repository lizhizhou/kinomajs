/*
 *    Copyright (C) 2010-2015 Marvell International Ltd.
 *    Copyright (C) 2002-2010 Kinoma, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
#define __FSKBITMAP_PRIV__	/**< To get access to the FskBitmap data structure */
#define __FSKCANVAS_PRIV__	/**< To get access to the state stack. */

#include "FskCanvas.h"

#include <math.h>
#include <stdio.h>

#include "FskCompositeRect.h"
#include "FskGlyphPath.h"
#include "FskGraphics.h"
#include "FskGrowableStorage.h"
#include "FskImage.h"
#include "FskMatrix.h"
#include "FskMemory.h"
#include "FskPick.h"
#include "FskPixelOps.h"
#include "FskPremultipliedAlpha.h"
#include "FskProjectImage.h"
#include "FskShadow.h"
#include "FskTransferAlphaBitmap.h"



/************************************************ Debugging configuration ************************************************/
#if SUPPORT_INSTRUMENTATION
	#define LOG_PARAMETERS			/**< Log the parameters of API calls. */
#endif /* SUPPORT_INSTRUMENTATION */

//#define CANVAS_DEBUG 1
#ifndef CANVAS_DEBUG
	#define CANVAS_DEBUG 0				/**< Turn off extra debugging logs. */
#endif /* CANVAS_DEBUG */
#if	defined(LOG_PARAMETERS)
	#undef  CANVAS_DEBUG
	#define CANVAS_DEBUG 1
#endif /* LOG_PARAMETERS et al */

#if SUPPORT_INSTRUMENTATION
	#ifndef CANVAS_DEBUG
		#define CANVAS_DEBUG 1
	#endif /* CANVAS_DEBUG */
	#define FskInstrumentedGlobalType(type)			(&(g##type##TypeInstrumentation))												/**< The data structure created with FskInstrumentedSimpleType(). */
	#define FskInstrumentedTypeEnabled(type, level)	FskInstrumentedTypeHasListenersForLevel(FskInstrumentedGlobalType(type), level)	/**< Whether instrumentation will print anything. */
#else /* !SUPPORT_INSTRUMENTATION */
	#define FskInstrumentedTypeEnabled(type, level)	(0)																				/**< Instrumentation will not print anything. */
#endif /* SUPPORT_INSTRUMENTATION */

FskInstrumentedSimpleType(Canvas, canvas);													/**< This declares the types needed for instrumentation. */

#if CANVAS_DEBUG
	#define	LOGD(...)	FskCanvasPrintfDebug(__VA_ARGS__)									/**< Print debugging logs. */
	#define	LOGI(...)	FskCanvasPrintfVerbose(__VA_ARGS__)									/**< Print information logs. */
#endif	/* CANVAS_DEBUG */
#define		LOGE(...)	FskCanvasPrintfMinimal(__VA_ARGS__)									/**< Print error logs always, when instrumentation is on. */
#ifndef     LOGD
	#define LOGD(...)																		/**< Don't print debugging logs. */
#endif   /* LOGD */
#ifndef     LOGI
	#define LOGI(...)																		/**< Don't print information logs. */
#endif   /* LOGI */
#ifndef     LOGE
	#define LOGE(...)																		/**< Don't print error logs. */
#endif   /* LOGE */
#define LOGD_ENABLED()	FskInstrumentedTypeEnabled(Canvas, kFskInstrumentationLevelDebug)	/**< Whether LOGD() will print anything. */
#define LOGI_ENABLED()	FskInstrumentedTypeEnabled(Canvas, kFskInstrumentationLevelVerbose)	/**< Whether LOGI() will print anything. */
#define LOGE_ENABLED()	FskInstrumentedTypeEnabled(Canvas, kFskInstrumentationLevelMinimal)	/**< Whether LOGE() will print anything. */



/*******************************************************************************
 *******************************************************************************
 *******************************************************************************
 ****						Typedefs and Macros 							****
 *******************************************************************************
 *******************************************************************************
 *******************************************************************************/

#define kFskFixedOne				(1 << 16)										/**< one, as represented in fixed point. */
#define D_PI						3.1415926535897932385							/**< pi       */
#define D_2PI						6.2831853071795864769							/**< pi * 2   */
#define	D_2PI_3						2.0943951023931954923							/**< pi * 2/3 */
#define kFixedOne					(1 << 16)										/**< one, as represented in fixed point. */
#define FskRoundFloatToInt(x)		((int)((x) + (((x) >= 0) ? 0.5 : -0.5)))		/**< Round a float to an int. \param[in] x floating-point input. \return the integer closest to the input x. */
#define FskIntToFloat(x)			(double)(x)										/**< convert an integer to floating-point. \param[in] x the input integer. \return the equivalent floating-point number. */
#define MIN_S32						((SInt32)0x80000000)							/**< -2147483648, the minimum number representable in SInt32. */
#define MAX_S32						((SInt32)0x7FFFFFFF)							/**< +2147483647, the maximum number representable in SInt32. */
#define MAX_RECT_SIZE				((SInt32)0x7FFFFFFF)							/**< +2147483647, the maximum size (width or height) representable in FskRectangleRecord. */
#define MIN_RECT_COORD				((SInt32)0xC0000000)							/**< -1073741824, the minimum coordinate representable in FskRectangleRecord. */
#define MAX_RECT_COORD				((SInt32)0x3FFFFFFF)							/**< +1073741823, the maximum coordinate representable in FskRectangleRecord. */

#ifdef _MSC_VER																		/**< Microsoft compatibility macros */
	#define __FLT_EVAL_METHOD__ 2													/**< All computation is done in long double */
	#define hypot(x,y)					_hypot(x,y)									/**< A Microsoft-names version for the standard hypot() function. */
	#define strncpy(to, fr, size)		strncpy_s(to, size, fr, _TRUNCATE)			/**< "safe" version on Windows always NULL-terminates */
#endif /* _MSC_VER */



/*******************************************************************************
 *******************************************************************************
 *******************************************************************************
 ****							Support Routines 							****
 *******************************************************************************
 *******************************************************************************
 *******************************************************************************/

#if CANVAS_DEBUG

static void LogBitmap(FskConstBitmap bm, const char *name) {
	if (!bm)
		return;
	if (!name)
		name = "BM";
	LOGD("\t%s: bounds(%d, %d, %d, %d), depth=%u, format=%s, rowBytes=%d, bits=%p, alpha=%d, premul=%d",
		name, (int)bm->bounds.x, (int)bm->bounds.y, (int)bm->bounds.width, (int)bm->bounds.height, (unsigned)bm->depth,
		FskBitmapFormatName(bm->pixelFormat), (int)bm->rowBytes, bm->bits, bm->hasAlpha, bm->alphaIsPremultiplied);
}

static void LogRect(FskConstRectangle r, const char *name) {
	if (!r)
		return;
	if (!name)
		name = "RECT";
	LOGD("\t%s(%d, %d, %d, %d)", name, (int)r->x, (int)r->y, (int)r->width, (int)r->height);
}
#endif /* CANVAS_DEBUG */


/** A data structure to overlay a 64-bit integer and 64-bit floating-point in the same memory location. */
union Uflint64 {
	FskInt64	i;	/**< A 64-bit integer. */
	double		d;	/**< A 64-bit floating-point number. */
};
/*												6554443322211000	*/
/*												0628406284062840	*/
static const union Uflint64	gNaN64      = {   0x7FF8000000000000LL };	/**< A double-precision NaN with no payload. */
static const union Uflint64	gInfinity64 = {   0x7FF0000000000000LL };	/**< A double-precision infinity. */


/****************************************************************************//**
 * A double-precision quiet NaN without payload.
 *	\return	A double-precision quiet NaN without payload.
 ********************************************************************************/

static double NaN64() {
	return gNaN64.d;
}


/****************************************************************************//**
 * Check whether a number is finite, i.e. neither infinity nor NaN.
 ********************************************************************************/

static Boolean IsFinite(double x) {
	union Uflint64 y;
	y.d = x;
	return gInfinity64.i != (y.i & gInfinity64.i);
}


/****************************************************************************//**
 * Check whether a number is finite and not negative.
 ********************************************************************************/

static Boolean IsFiniteNonnegative(double x) {
	return IsFinite(x) && x >= 0;
}


/****************************************************************************//**
 * Fix Up a Canvas2d Color Source Gradient Pointer.
 *	\param[in,out]	the color source.
 ********************************************************************************/

static void FixUpCanvas2dColorSourceGradientPointer(FskCanvas2dColorSource *dst) {
	dst->csu.lg.gradientStops = dst->gs;
	switch (dst->csu.so.type) {
		case kFskColorSourceTypeLinearGradient:	dst->csu.lg.gradientStops = dst->gs;	break;
		case kFskColorSourceTypeRadialGradient:	dst->csu.rg.gradientStops = dst->gs;	break;
	}
}


/****************************************************************************//**
 * Fix Up the Canvas Context Pointers.
 *	\param[in,out]	the Canvas context.
 ********************************************************************************/

static void FixUpCanvasContextPointers(FskCanvas2dContext ctx) {
	FskCanvas2dContextState *st;
	UInt32 n;
	if (!ctx)
		return;
	n = FskGrowableArrayGetItemCount(ctx->state);
	if (!FskGrowableArrayGetPointerToItem(ctx->state, 0, (void**)(void*)&st))
		return;
	for (; n--; ++st) {
		FixUpCanvas2dColorSourceGradientPointer(&st->strokeStyle);
		FixUpCanvas2dColorSourceGradientPointer(&st->fillStyle);
		st->font.family = st->fontFamily;
	}
}


/****************************************************************************//**
 * Get the Canvas State From the Canvas Context.
 *	\param[in,out]	the Canvas context.
 *	\return			the equivalent Canvas2d context state.
 ********************************************************************************/

FskCanvas2dContextState* FskCanvasGetStateFromContext(FskCanvas2dContext ctx) {
	FskCanvas2dContextState *st = NULL;
	if (ctx) FskGrowableArrayGetPointerToLastItem(ctx->state, (void**)(void*)&st);
	return st;
}


/****************************************************************************//**
 * Get an immutable Canvas State From the Canvas Context.
 *	\param[in,out]	the Canvas context.
 *	\return			the equivalent Canvas2d context state.
 ********************************************************************************/

const FskCanvas2dContextState* FskCanvasGetConstStateFromContext(FskConstCanvas2dContext ctx) {
	FskCanvas2dContextState *st = NULL;
	if (ctx) FskGrowableArrayGetConstPointerToLastItem(ctx->state, (const void**)(void*)&st);
	return st;
}


/****************************************************************************//**
 * Set a Fixed Point Matrix From a Double Precision Matrix
 ********************************************************************************/

static void SetFixedFromDoubleMatrix(FskFixedMatrix3x2 *X, FskCanvasMatrix3x2d *D) {
	int			n	= 3 * 2;
	FskFixed	*x	= X->M[0];
	double		*d	= D->M[0];

	for (; n--; ++x, ++d)
		*x = FskRoundFloatToFixed(*d);
}


/****************************************************************************//**
 * Set the value of a 3x2 Matrix.
 *	\param[out]	T	the 3x3 matrix.
 *	\param[in]	a	the value to be assigned to matrix element M[0][0].
 *	\param[in]	b	the value to be assigned to matrix element M[0][1].
 *	\param[in]	c	the value to be assigned to matrix element M[1][0].
 *	\param[in]	d	the value to be assigned to matrix element M[1][1].
 *	\param[in]	e	the value to be assigned to matrix element M[2][0].
 *	\param[in]	f	the value to be assigned to matrix element M[2][1].
 ********************************************************************************/

#define SetMatrix3x2(T, a, b, c, d, e, f)	do { (T)->M[0][0] = (a); (T)->M[0][1] = (b); (T)->M[1][0] = (c); (T)->M[1][1] = (d); (T)->M[2][0] = (e); (T)->M[2][1] = (f); } while(0)


/****************************************************************************//**
 * Multiply Two 3x2 Matrices.
 * This works in-place, i.e. when L==P or R==P.
 *	\param[in]	L	the left  matrix.
 *	\param[in]	R	the right matrix.
 *	\param[out]	P	the result matrix. This can be the same as L or R.
 ********************************************************************************/

static void MultiplyMatrices3x2(const FskCanvasMatrix3x2d *L, const FskCanvasMatrix3x2d *R, FskCanvasMatrix3x2d *P) {
	FskCanvasMatrix3x2d U;
#if defined(__FLT_EVAL_METHOD__) & __FLT_EVAL_METHOD__ < 2
	U.M[0][0] = L->M[0][0] * R->M[0][0] + L->M[0][1] * R->M[1][0];					U.M[0][1] = L->M[0][0] * R->M[0][1] + L->M[0][1] * R->M[1][1];
	U.M[1][0] = L->M[1][0] * R->M[0][0] + L->M[1][1] * R->M[1][0];					U.M[1][1] = L->M[1][0] * R->M[0][1] + L->M[1][1] * R->M[1][1];
	U.M[2][0] = L->M[2][0] * R->M[0][0] + L->M[2][1] * R->M[1][0] + R->M[2][0];	U.M[2][1] = L->M[2][0] * R->M[0][1] + L->M[2][1] * R->M[1][1] + R->M[2][1];
#else /* __FLT_EVAL_METHOD__ == 2 */
	U.M[0][0] = L->M[0][0] * R->M[0][0];	U.M[0][0] += L->M[0][1] * R->M[1][0];
	U.M[0][1] = L->M[0][0] * R->M[0][1];	U.M[0][1] += L->M[0][1] * R->M[1][1];
	U.M[1][0] = L->M[1][0] * R->M[0][0];	U.M[1][0] += L->M[1][1] * R->M[1][0];
	U.M[1][1] = L->M[1][0] * R->M[0][1];	U.M[1][1] += L->M[1][1] * R->M[1][1];
	U.M[2][0] = L->M[2][0] * R->M[0][0];	U.M[2][0] += L->M[2][1] * R->M[1][0];	U.M[2][0] += R->M[2][0];
	U.M[2][1] = L->M[2][0] * R->M[0][1];	U.M[2][1] += L->M[2][1] * R->M[1][1];	U.M[2][1] += R->M[2][1];
#endif
	*P = U;
}


/****************************************************************************//**
 * Initialize the Canvas State.
 *	\param[out]	st	the Canvas state.
 *	\param[in]	width	the desired width  of the canvas.
 *	\param[in]	height	the desired height of the canvas.
 ********************************************************************************/

static void InitCanvasState(FskCanvas2dContextState *st, SInt32 width, SInt32 height) {
	if (st == NULL || width <= 0 || height <= 0)
		return;
	FskMemSet(st, 0, sizeof(FskCanvas2dContextState));	/* Make sure EVERYTHING is clear */
	FixUpCanvas2dColorSourceGradientPointer(&st->strokeStyle);
	FixUpCanvas2dColorSourceGradientPointer(&st->fillStyle);
	st->font.family = st->fontFamily;

    st->globalAlpha					= 255U;
	st->lineCap						= kFskCanvas2dLineCapDefault;
	st->lineJoin					= kFskCanvas2dLineJoinDefault;
    st->textAlign					= kFskCanvas2dTextAlignDefault;
    st->textBaseline				= kFskCanvas2dTextBaselineDefault;
	st->globalCompositeOperation	= kFskCompositePreSourceOver;
	st->fillRule					= kFskCanvas2dFillRuleWindingNumber;
	st->quality						= 1;
	st->lineWidth					= kFskFixedOne;
	st->miterLimit					= FskRoundFloatToFixed(10);
	st->shadowOffset.x				= FskRoundFloatToInt(0);
	st->shadowOffset.y				= FskRoundFloatToInt(0);
	st->shadowBlur					= 0.f;
	FskRectangleSet(&st->clipRect, 0, 0, width, height);
	SetMatrix3x2(&st->transform, 1., 0., 0., 1., 0., 0.);
	FskSetBasicConstantColorSource(&st->strokeStyle.csu.cn, 0, 0, 0, 255);	/* Opaque black */
	FskSetBasicConstantColorSource(&st->fillStyle.csu.cn,   0, 0, 0, 255);	/* Opaque black */
	FskColorRGBASet(               &st->shadowColor,		0, 0, 0, 0);	/* Transparent black */

	strncpy(st->fontFamily, "sans-serif", sizeof(st->fontFamily));
	st->font.size		= 10.;
	st->font.weight		= kFskFontWeightNormal;
	st->font.style		= kFskFontStyleNormal;
	st->font.anchor		= kFskTextAnchorStart;
	st->font.stretch	= kFskFontStretchNormal;
	st->font.decoration	= kFskFontDecorationNone;
	st->font.variant	= kFskFontVariantNormal;
	st->font.sizeAdjust	= 0;
}


/****************************************************************************//**
 * Clean up the Canvas State.
 *	\param[in,out]	st	the Canvas state.
 ********************************************************************************/

static void CleanupCanvasState(FskCanvas2dContextState *st)
{
	if (st && st->clipBM) {
		FskBitmapDispose(st->clipBM);
		st->clipBM = NULL;
	}
}


/****************************************************************************//**
 * Clear the Buffer To a Particular Value.
 *	\param[in,out]	bm		the bitmap buffer.
 *	\param[in]		value	the clear value.
 ********************************************************************************/

static void ClearBufferToValue(FskBitmap bm, UInt8 value) {
	char *pix;
	SInt32 rowBytes;
	if (!bm || !bm->depth || kFskErrNone != FskBitmapWriteBegin(bm, (void**)(void*)&pix, &rowBytes, NULL) || bm->bounds.height <= 0)
		return;
	if (rowBytes < 0) {
		rowBytes = -rowBytes;
		pix -= (UInt32)rowBytes * (UInt32)(bm->bounds.height - 1);
	}
	FskMemSet(pix, value, (UInt32)rowBytes * (UInt32)(bm->bounds.height));
	FskBitmapWriteEnd(bm);
}


#ifdef UNUSED
/********************************************************************************
 * ShuffleBitmapData
 ********************************************************************************/

static void ShuffleBitmapData(void		*baseAddr,	UInt8	fillValue,	UInt32	pixBytes,
							  UInt32	oldWidth,	UInt32	oldHeight,	SInt32	oldRowBytes,
							  UInt32	newWidth,	UInt32	newHeight,	SInt32	newRowBytes)
{
	const UInt8		*s				= baseAddr;
	UInt8			*d				= baseAddr;
	UInt32			w, h, width, height, pixLineBytes;

	/* Shuffle data */
	width  = (oldWidth  < newWidth)  ? oldWidth  : newWidth;
	height = (oldHeight < newHeight) ? oldHeight : newHeight;
	pixLineBytes = pixBytes * width;
	if (oldRowBytes != newRowBytes) {
		if (abs(newRowBytes) < abs(oldRowBytes)) {
			for (h = height; h--; s += oldRowBytes, d += newRowBytes)
				 FskMemMove(d, s, pixLineBytes);
		}
		else {
			s += (height - 1) * oldRowBytes;
			d += (height - 1) * newRowBytes;
			for (h = height; h--; s -= oldRowBytes, d -= newRowBytes)
				FskMemMove(d, s, pixLineBytes);
		}
	}

	/* Clear out crud */
	if (pixLineBytes < newRowBytes) {	/* Clear crud on the right */
		for (h = height, d = (UInt8*)baseAddr + pixLineBytes, w = newRowBytes - pixLineBytes; h--; d += newRowBytes)
			FskMemSet(d, fillValue, w);
	}
	if (newHeight > height)				/* Clear crud on the bottom */
		FskMemSet((UInt8*)baseAddr + height * newRowBytes, fillValue, (newHeight - height) * newRowBytes);
}


/********************************************************************************
 * NondestructiveReshapeBitmap
 ********************************************************************************/

static FskErr NondestructiveReshapeClipmap(UInt32 width, UInt32 height, FskBitmap *bmp, UInt32 *maxBytes) {
	FskErr		err			= kFskErrNone;
	FskBitmap	bm;
	SInt32		rowBytes;
	UInt32		needBytes;

	BAIL_IF_NULL((bmp), err, kFskErrInvalidParameter);
	bm = *bmp;

	rowBytes = (width + 3) & ~3;				/* Bump up to a multiple of 4 */
	needBytes = (UInt32)rowBytes * height;

	if (needBytes > *maxBytes || bm == NULL) {	/* Need more bytes than we have; need to reallocate */
		BAIL_IF_ERR(err = FskBitmapNew(width, height, kFskBitmapFormat8G, bmp));
		ClearBufferToValue(*bmp, 0);
		BAIL_IF_ERR(err = FskBitmapDraw(bm, &bm->bounds, *bmp, &bm->bounds, NULL, NULL, kFskGraphicsModeCopy, NULL));
		FskBitmapDispose(bm);
		*maxBytes = rowBytes * height;
	}
	else {										/* Can reshape in place */
		if (bm->rowBytes < 0)
			rowBytes = -rowBytes;
		ShuffleBitmapData(bm->bits,	0, 1, bm->bounds.width,	bm->bounds.height, bm->rowBytes, width, height, rowBytes);
		bm->bounds.width  = width;
		bm->bounds.height = height;
		bm->rowBytes      = rowBytes;
	}

bail:
	return err;
}
#endif /* UNUSED */


/****************************************************************************//**
 * Reshape a Bitmap.
 * This is destructive.
 *	\param[in]	width		the desired bitmap width.
 *	\param[in]	height		the desired bitmap height.
 *	\param[in]	pixelFormat	the desired bitmap pixel format.
 *	\param[in]	bmp			the bitmap to be reshaped.
 *	\param[in]	maxBytes	On input, the maximum number of bytes already allocated to the bitmap.
 *							On output, the maximum number of bytes already allocated to the bitmap.
 *							The input and output values may be different if reallocation needed to be done.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr ReshapeBitmap(UInt32 width, UInt32 height, FskBitmapFormatEnum pixelFormat, FskBitmap *bmp, UInt32 *maxBytes) {
	FskErr		err			= kFskErrNone;
	FskBitmap	bm;
	SInt32		pixBytes, rowBytes;
	UInt32		needBytes;

	BAIL_IF_NULL((bmp), err, kFskErrInvalidParameter);
	bm = *bmp;
	if ((int)pixelFormat <= 0 && bm) {
		pixelFormat = bm->pixelFormat;
		pixBytes = bm->depth >> 3;
	}
	else {
		pixBytes = FskBitmapFormatPixelBytes(pixelFormat);
	}

	rowBytes = (pixBytes * width + 3) & ~3;									/* Bump up to a multiple of 4 */
	needBytes = (UInt32)rowBytes * height;

	if (needBytes > *maxBytes || bm == NULL) {								/* Need more bytes than we have; need to reallocate */
		FskBitmapDispose(bm);
		*bmp = NULL;
		*maxBytes = 0;
		BAIL_IF_ERR(err = FskBitmapNew(width, height, pixelFormat, bmp));
		bm = *bmp;
		if ((rowBytes = bm->rowBytes) < 0)
			rowBytes = -rowBytes;
		*maxBytes = rowBytes * height;
		if (bm->depth == 32) {
			FskBitmapSetHasAlpha(bm, true);
			FskBitmapSetAlphaIsPremultiplied(bm, true);
		}
	}
	else {																	/* Can reshape in place */
		bm->bounds.width  = width;
		bm->bounds.height = height;
		bm->rowBytes = (bm->rowBytes < 0) ? -rowBytes : rowBytes;
	}

bail:
	return err;
}


/****************************************************************************//**
 * Reset the Canvas Context.
 *	\param[in,out]	ctx		the Canvas context to be reset.
 *	\return			kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr ResetCanvasContext(FskCanvas2dContext ctx)
{
	FskErr		err		= kFskErrNone;
	FskCanvas	cvs;
	int			i;
	FskBitmap	bm;
	FskCanvas2dContextState *st;

	BAIL_IF_FALSE(ctx && (cvs = ctx->canvas), err, kFskErrInvalidParameter);
	for (i = FskGrowableArrayGetItemCount(ctx->state); --i;)
		err = FskCanvas2dRestore(ctx);
	BAIL_IF_ERR(err);
	BAIL_IF_ERR(err = FskGrowableArraySetItemCount(ctx->state, 1));
	FskGrowablePathClear(ctx->path);
	st = FskCanvasGetStateFromContext(ctx);
	BAIL_IF_NULL(st, err, kFskErrBadData);
	bm = cvs->bm;
	if (bm) {
		InitCanvasState(st, bm->bounds.width, bm->bounds.height);
		if (st->clipBM) {
			ReshapeBitmap(bm->bounds.width, bm->bounds.height, kFskBitmapFormatUnknown, &st->clipBM, &st->clipBytes);	/* kFskBitmapFormatUnknown --> keep the same format */
			ClearBufferToValue(st->clipBM, 255U);
		}
		if (cvs->tmp)
			ReshapeBitmap(bm->bounds.width, bm->bounds.height, bm->pixelFormat, &cvs->tmp, &cvs->tmpBytes);
	}
	else {
		InitCanvasState(st, 32767, 32767);
	}
	FskCanvas2dSetTransform(ctx, 1., 0., 0., 1., 0., 0.);	/* Assure that transform, device transform and fixed transform are reset. */

bail:
	return err;
}


/****************************************************************************//**
 * Initialize the Canvas Context.
 *	\param[in,out]	ctx		the Canvas context to be reset.
 *	\param[in]		canvas		the Canvas.
 *	\param[in]		stackSize		the expected maximum size required for the stack.
 *	\return			kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr InitCanvasContext(FskCanvas2dContext ctx, FskCanvas canvas, UInt32 stackSize)
{
	FskErr err = kFskErrNone;

	BAIL_IF_FALSE(ctx && canvas, err, kFskErrInvalidParameter);
	ctx->state  = NULL;
	ctx->canvas = NULL;
	if (stackSize < 1)
		stackSize = 1;
	BAIL_IF_ERR(err = FskGrowableArrayNew(sizeof(FskCanvas2dContextState), stackSize, &ctx->state));
	BAIL_IF_ERR(err = FskGrowableArraySetItemCount(ctx->state, 1));
	BAIL_IF_ERR(err = FskGrowablePathNew(0, &ctx->path));
	ctx->canvas = canvas;

	SetMatrix3x2(&ctx->deviceTransform, 1., 0., 0., 1., 0., 0.);
	BAIL_IF_ERR(err = ResetCanvasContext(ctx));					/* This needs to have ctx->canvas set */

bail:
	return err;
}


/****************************************************************************//**
 * CleanupCanvasContext
 *	\param[in,out]	ctx		the Canvas context to be cleaned up.
 ********************************************************************************/

static void CleanupCanvasContext(FskCanvas2dContext ctx)
{
	if (ctx) {
		FskGrowablePathDispose(ctx->path);
		if (ctx->state) {
			UInt32 i = FskGrowableArrayGetItemCount(ctx->state);
			while (i--) {
				FskCanvas2dContextState *st;
				if (FskGrowableArrayGetPointerToItem(ctx->state, i, (void**)(void*)(&st)) == kFskErrNone)
					CleanupCanvasState(st);
			}
			FskGrowableArrayDispose(ctx->state);
			ctx->state = NULL;
		}
		ctx->canvas = NULL;
	}
}


/****************************************************************************//**
 * Set the Canvas2d Color Source.
 *	\param[in]	srcCS		the source color source.
 *	\param[out]	dst			the destination color source.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr SetCanvas2dColorSource(const FskColorSource *srcCS, FskCanvas2dColorSource *dst)
{
	FskErr	err = kFskErrNone;
	FskColorSourceUnion *src = (FskColorSourceUnion*)srcCS;

	BAIL_IF_FALSE(srcCS && dst, err, kFskErrInvalidParameter);
	switch (src->so.type) {
		case kFskColorSourceTypeConstant:
			dst->csu.cn = src->cn;
			break;

		case kFskColorSourceTypeLinearGradient:
			dst->csu.lg = src->lg;
			if (dst->csu.lg.numStops > kCanvas2DMaxGradientStops) {
				dst->csu.lg.numStops = kCanvas2DMaxGradientStops;
				err = kFskErrTooMany;
			}
			if (dst->csu.lg.gradientMatrix) {
				dst->csu.lg.gradientMatrix = NULL;
				err = kFskErrUnimplemented;
			}
			dst->csu.lg.gradientStops = dst->gs;
			FskMemCopy(dst->csu.lg.gradientStops, src->lg.gradientStops, dst->csu.lg.numStops * sizeof(*dst->csu.lg.gradientStops));
			break;

		case kFskColorSourceTypeRadialGradient:
			dst->csu.rg = src->rg;
			if (dst->csu.rg.numStops > kCanvas2DMaxGradientStops) {
				dst->csu.rg.numStops = kCanvas2DMaxGradientStops;
				err = kFskErrTooMany;
			}
			if (dst->csu.rg.gradientMatrix) {
				dst->csu.rg.gradientMatrix = NULL;
				err = kFskErrUnimplemented;
			}
			dst->csu.rg.gradientStops = dst->gs;
			FskMemCopy(dst->csu.rg.gradientStops, src->rg.gradientStops, dst->csu.rg.numStops * sizeof(*dst->csu.rg.gradientStops));
			break;

		case kFskColorSourceTypeTexture:
			dst->csu.tx = src->tx;
			if (dst->csu.tx.textureFrame) {
				dst->csu.tx.textureFrame = NULL;
				err = kFskErrUnimplemented;
			}
			break;

		case kFskColorSourceTypeProcedure:
			dst->csu.pr = src->pr;
			break;
		default:
			BAIL_IF_TRUE(true, err, kFskErrInvalidParameter);
	}

	if (dst->csu.so.dashCycles != 0) {
		dst->csu.so.dashCycles	= 0;
		err = kFskErrUnimplemented;
	}
	dst->csu.so.dash = NULL;
	dst->csu.so.dashPhase = 0;

bail:
	return err;
}


/****************************************************************************//**
 * Set the Canvas2d Color Source Type.
 * This resets the dash parameters.
 *	\param[out]	cs	the color source.
 *	\param[in]	type	the desired type for he color source.
 ********************************************************************************/

static void SetCanvas2dColorSourceType(FskCanvas2dColorSource *cs, UInt32 type)
{
	if (!cs)
		return;
	cs->csu.so.type			= type;
	cs->csu.so.dashCycles	= 0;
	cs->csu.so.dash			= NULL;
	cs->csu.so.dashPhase	= 0;
	switch (type) {
		case kFskColorSourceTypeLinearGradient:	cs->csu.lg.gradientStops = cs->gs;	break;	/* These should both point to the same location */
		case kFskColorSourceTypeRadialGradient:	cs->csu.rg.gradientStops = cs->gs;	break;
	}
}


/****************************************************************************//**
 * Set the Canvas2d Color Source to a Solid Color.
 *	\param[out]	cs		the color source to be changed.
 *	\param[in]	color	the desired color for the color source.
 ********************************************************************************/

static void SetCanvas2dColorSourceColor(FskCanvas2dColorSource *cs, FskConstColorRGBA color)
{
	if (!cs || !color)
		return;
	SetCanvas2dColorSourceType(cs, kFskColorSourceTypeConstant);
	cs->csu.cn.color = *color;
}


/****************************************************************************//**
 * Set the Canvas2d Gradient Stops.
 *	\param[in]	numStops	the number of stops.
 *	\param[in]	src			the source gradient stops.
 *	\param[out]	dst			the destination gradient stops.
 ********************************************************************************/

static void SetCanvas2dGradientStops(UInt32 numStops, const FskCanvas2dGradientStop *src, FskGradientStop *dst) {
	if (!src || !dst)
		return;
	for (; numStops--; ++src, ++dst) {
		dst->offset = FskRoundFloatToFract(src->offset);
		dst->color  = src->color;
	}
}


/****************************************************************************//**
 * Set the Canvas2d Color Source to a Linear Gradient.
 *	\param[out]	cs			the color source to be set.
 *	\param[in]	x0			the X-coordinate of the starting point of the linear gradient.
 *	\param[in]	y0			the Y-coordinate of the starting point of the linear gradient.
 *	\param[in]	x1			the X-coordinate of the  ending  point of the linear gradient.
 *	\param[in]	y1			the Y-coordinate of the  ending  point of the linear gradient.
 *	\param[in]	numStops	the number of stops in the gradient.
 *	\param[in]	stops		the stops in the gradient.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr SetCanvas2dColorSourceLinearGradient(FskCanvas2dColorSource *cs,
												   double x0, double y0, double x1, double y1,
												   UInt32 numStops, const FskCanvas2dGradientStop *stops) {
	FskErr err = kFskErrNone;

	if (!cs || !stops || !IsFinite(x0) || !IsFinite(y0) || !IsFinite(x1) || !IsFinite(y1))
		return kFskErrInvalidParameter;
	if (numStops > kCanvas2DMaxGradientStops) {
		numStops = kCanvas2DMaxGradientStops;
		err = kFskErrTooMany;
	}

	FixUpCanvas2dColorSourceGradientPointer(cs);
	SetCanvas2dColorSourceType(cs, kFskColorSourceTypeLinearGradient);
	cs->csu.lg.spreadMethod        = kFskSpreadPad;
	cs->csu.lg.gradientMatrix      = NULL;
	cs->csu.lg.gradientFracBits    = 16;
	cs->csu.lg.gradientVector[0].x = FskRoundFloatToFixed(x0);
	cs->csu.lg.gradientVector[0].y = FskRoundFloatToFixed(y0);
	cs->csu.lg.gradientVector[1].x = FskRoundFloatToFixed(x1);
	cs->csu.lg.gradientVector[1].y = FskRoundFloatToFixed(y1);
	cs->csu.lg.numStops            = numStops;
	SetCanvas2dGradientStops(cs->csu.lg.numStops, stops, cs->csu.lg.gradientStops);

	return err;
}


/********************************************************************************
 * Set the Canvas2d Color Source to a Radial Gradient.
 *	\param[out]	cs			the color source to be set.
 *	\param[in]	x0			the X-coordinate of the focus point of the radial gradient.
 *	\param[in]	y0			the Y-coordinate of the focus point of the radial gradient.
 *	\param[in]	r0			the focus radius of the radial gradient.
 *	\param[in]	x1			the X-coordinate of the center point of the radial gradient.
 *	\param[in]	y1			the Y-coordinate of the center point of the radial gradient.
 *	\param[in]	r1			the center radius of the radial gradient.
 *	\param[in]	numStops	the number of stops in the gradient.
 *	\param[in]	stops		the stops in the gradient.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr SetCanvas2dColorSourceRadialGradient(FskCanvas2dColorSource *cs,
												   double x0, double y0, double r0, double x1, double y1, double r1,
												   UInt32 numStops, const FskCanvas2dGradientStop *stops) {
	FskErr err = kFskErrNone;

	if (!cs || !stops || !IsFinite(x0) || !IsFinite(y0) || !IsFiniteNonnegative(r0) || !IsFinite(x1) || !IsFinite(y1) || !IsFiniteNonnegative(r1) )
		return kFskErrInvalidParameter;
	if (numStops > kCanvas2DMaxGradientStops) {
		numStops = kCanvas2DMaxGradientStops;
		err = kFskErrTooMany;
	}

	FixUpCanvas2dColorSourceGradientPointer(cs);
	SetCanvas2dColorSourceType(cs, kFskColorSourceTypeRadialGradient);
	cs->csu.rg.spreadMethod		= kFskSpreadPad;
	cs->csu.rg.gradientMatrix	= NULL;
	cs->csu.rg.gradientFracBits	= 16;
	cs->csu.rg.focus.x			= FskRoundFloatToFixed(x0);
	cs->csu.rg.focus.y			= FskRoundFloatToFixed(y0);
	cs->csu.rg.center.x			= FskRoundFloatToFixed(x1);
	cs->csu.rg.center.y			= FskRoundFloatToFixed(y1);
	cs->csu.rg.focalRadius		= FskRoundFloatToFixed(r0);
	cs->csu.rg.radius			= FskRoundFloatToFixed(r1);
	cs->csu.rg.numStops			= numStops;
	SetCanvas2dGradientStops(cs->csu.rg.numStops, stops, cs->csu.rg.gradientStops);

	return err;
}


#if SRC_32A16RGB565LE
/****************************************************************************//**
 * FskConvert a kFskBitmapFormat32A16RGB565LE Bitmap to kFskBitmapFormatDefaultRGBA.
 *	\param[in,out]	bm			the bitmap to be converted.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static void FskConvert32A16BitmapToDefault32(FskBitmap bm) {
	UInt32	*p	= NULL;
	FskErr	err;
	SInt32	wd, bump, w, h;
	UInt32	pix;

	BAIL_IF_ERR(err = FskBitmapWriteBegin(bm, (void**)(void*)&p, &bump, NULL));
	wd = bm->bounds.width;
	h  = bm->bounds.height;
	bump = (bump - wd * (bm->depth >> 3)) / sizeof(*p);
	switch (kFskBitmapFormatDefaultRGBA) {
		case FskName2(kFskBitmapFormat,fsk32ABGRKindFormat):	for (; h--; p+=bump) for (w=wd; w--; ++p) { pix=*p; fskConvert32A16RGB565SE32ABGR(pix); *p=pix; } break;
		case FskName2(kFskBitmapFormat,fsk32ARGBKindFormat):	for (; h--; p+=bump) for (w=wd; w--; ++p) { pix=*p; fskConvert32A16RGB565SE32ARGB(pix); *p=pix; } break;
		case FskName2(kFskBitmapFormat,fsk32BGRAKindFormat):	for (; h--; p+=bump) for (w=wd; w--; ++p) { pix=*p; fskConvert32A16RGB565SE32BGRA(pix); *p=pix; } break;
		case FskName2(kFskBitmapFormat,fsk32RGBAKindFormat):	for (; h--; p+=bump) for (w=wd; w--; ++p) { pix=*p; fskConvert32A16RGB565SE32RGBA(pix); *p=pix; } break;
		default:												BAIL(kFskErrUnsupportedPixelType);
	}
	bm->pixelFormat = kFskBitmapFormatDefaultRGBA;
bail:
	if (p)	FskBitmapWriteEnd(bm);
	return;
}
#endif /* SRC_32A16RGB565LE */


/****************************************************************************//**
 * Set the Canvas2d Color Source to a Pattern
 *	\param[out]	cs			the color source to be set.
 *	\param[in]	repetition	the repetition enum for the pattern.
 *	\param[in]	texture		the texture of the pattern.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr SetCanvas2dColorSourcePattern(FskCanvas2dColorSource *cs, UInt32 repetition, FskConstBitmap texture) {
	if (!cs || !texture)
		return kFskErrInvalidParameter;
	SetCanvas2dColorSourceType(cs, kFskColorSourceTypeTexture);
	cs->csu.tx.spreadMethod	= repetition;
	cs->csu.tx.textureFrame	= NULL;
	cs->csu.tx.texture		= texture;
	#if SRC_32A16RGB565LE
		if (texture->pixelFormat == FskName2(kFskBitmapFormat,fsk32A16RGB565SEKindFormat))
			FskConvert32A16BitmapToDefault32((FskBitmap)(cs->csu.tx.texture));		/* Premultiplied 32A16RGB565 looks horrible: prevent it */
	#endif /* SRC_32A16RGB565LE */
	FskStraightAlphaToPremultipliedAlpha((FskBitmap)(cs->csu.tx.texture), NULL);	// TODO: We shouldn't modify a const bitmap.
	return kFskErrNone;
}


/****************************************************************************//**
 * Get the Pointer to the Stroke Color Source.
 *	\param[in]	ctx	the Canvas context.
 *	\return		the requested color source pointer.
 ********************************************************************************/

static	FskCanvas2dColorSource*	GetStrokeColorSourcePtr(FskCanvas2dContext ctx)	{ return &FskCanvasGetStateFromContext(ctx)->strokeStyle;	}	/* private */


/****************************************************************************//**
 * Get the Pointer to the Fill Color Source.
 *	\param[in]	ctx	the Canvas context.
 *	\return		the requested color source pointer.
 ********************************************************************************/

static	FskCanvas2dColorSource*	GetFillColorSourcePtr(  FskCanvas2dContext ctx)	{ return &FskCanvasGetStateFromContext(ctx)->fillStyle;		}	/* private */



/****************************************************************************//**
 * Clone a bitmap: same size, format, and content.
 *	\param[in]	src	the source bitmap.
 *	\param[out]	dst	a place to store the resultant cloned bitmap.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr
FskBitmapClone(FskConstBitmap src, FskBitmap *dst)
{
	FskErr	err	= kFskErrNone;
	Boolean	val;

	BAIL_IF_NULL(dst, err, kFskErrInvalidParameter);
	*dst = NULL;
	if (src == NULL)
		goto bail;
	BAIL_IF_ERR(err = FskBitmapNew(src->bounds.width, src->bounds.height, src->pixelFormat, dst));
	FskBitmapGetHasAlpha(*dst, &val);				FskBitmapSetHasAlpha(*dst,  val);
	FskBitmapGetAlphaIsPremultiplied(*dst, &val);	FskBitmapSetAlphaIsPremultiplied(*dst,  val);
	BAIL_IF_ERR(err = FskBitmapDraw(src, NULL, *dst, NULL, NULL, NULL, kFskGraphicsModeCopy, NULL));

bail:

	return err;
}


/****************************************************************************//**
 * Clear the Canvas Temp Buffer To the Specified Value.
 *	\param[in,out]	ctx	the Canvas context.
 *	\param[in]		value	the clear value.
 ********************************************************************************/

static void ClearTempBufferToValue(FskCanvas2dContext ctx, UInt8 value) {
	if (!ctx || !ctx->canvas)
		return;
	if (ctx->canvas->tmp == NULL) {
		FskBitmap bm = ctx->canvas->bm;
		if (kFskErrNone != FskBitmapNew(bm->bounds.width, bm->bounds.height, bm->pixelFormat, &ctx->canvas->tmp))
			return;
		FskBitmapSetHasAlpha(ctx->canvas->tmp, true);
		FskBitmapSetAlphaIsPremultiplied(ctx->canvas->tmp, true);
	}
	ClearBufferToValue(ctx->canvas->tmp, value);
}


/****************************************************************************//**
 * Clear the Canvas Temp Buffer To Transparent Black.
 *	\param[in,out]	ctx	the Canvas context.
 ********************************************************************************/

static void ClearTempBufferTransparent(FskCanvas2dContext ctx) { ClearTempBufferToValue(ctx,   0U);	}


/****************************************************************************//**
 * Determine whether it is possible to Render Directly to the destination bitmap, given the current state.
 * If not, it is necessary to render to a temporary bitmap, then composite it.
 *	\param[in]	st	the Canvas state.
 *	\return		true,	if it is possible to render directly to the destination bitmap.
 *	\return		false,	if it is necessary to render to a temporary bitmap first, then composite it.
 ********************************************************************************/

Boolean FskCanvasCanRenderDirectly(const FskCanvas2dContextState *st) {
	return	(NULL == st->clipBM)																						/* No clipping */
		&&	(255  == st->globalAlpha)																					/* No global transparency */
		&&	(kFskCanvas2dCompositePreSourceOver == st->globalCompositeOperation)										/* Normal premultiplied alpha compositing */
		&&	((0U == st->shadowColor.a) || (0 == st->shadowOffset.x && 0 == st->shadowOffset.y && 0 == st->shadowBlur))	/* No shadow */
		;
}


/********************************************************************************
 * Determine whether the Color Source Is Opaque.
 *	\param[in]	cs	the color source to be queried.
 *	\return		true,	if the color source is opaque.
 *	\return		false,	if the color source is not opaque.
 ********************************************************************************/

static Boolean ColorSourceIsOpaque(const FskColorSource *cs) {
	Boolean val;
	unsigned i;
	const FskColorSourceUnion *csu = (const FskColorSourceUnion*)cs;
	switch (csu->so.type) {
		case kFskColorSourceTypeConstant:
			return csu->cn.color.a == 255;
		case kFskColorSourceTypeLinearGradient:
			for (i = csu->lg.numStops; i--; )
				if (csu->lg.gradientStops[i].color.a != 255)
					return false;
			return true;
		case kFskColorSourceTypeRadialGradient:
			for (i = csu->rg.numStops; i--; )
				if (csu->rg.gradientStops[i].color.a != 255)
					return false;
			return true;
		case kFskColorSourceTypeTexture:
			FskBitmapGetHasAlpha(csu->tx.texture, &val);
			return !val;
		default:
			return false;
	}
}


/****************************************************************************//**
 * Determine whether we Can Fill the path Directly, given the current state.
 *	\param[in]	st		the Canvas state.
 *	\return		true,	if it is possible to fill the path directly.
 *	\return		false,	if the path must first be filled in the temporary buffer, then composited.
 ********************************************************************************/

Boolean FskCanvasCanFillDirectly(const FskCanvas2dContextState *st) {
	return	FskCanvasCanRenderDirectly(st)
		&&	ColorSourceIsOpaque(&st->fillStyle.csu.so)		/* Opaque source */
		;

}


/********************************************************************************
 * Determine whether we Can Stroke the path Directly, given the current state.
 *	\param[in]	st		the Canvas state.
 *	\return		true,	if it is possible to stroke the path directly.
 *	\return		false,	if the path must first be stroked in the temporary buffer, then composited.
 ********************************************************************************/

Boolean FskCanvasCanStrokeDirectly(const FskCanvas2dContextState *st) {
	return	FskCanvasCanRenderDirectly(st)
		&&	ColorSourceIsOpaque(&st->strokeStyle.csu.so)	/* Opaque source */
		;
}


/****************************************************************************//**
 * Convert Fixed Bounds to Int Bounds.
 *	\param[in]	fixRect	the fixed point bounds.
 *	\param[out]	fixRect	the integer bounds.
 ********************************************************************************/

static void FixedToIntBounds(FskConstFixedRectangle fixRect, FskRectangle intRect) {
	/* We can use the bounding box for shadowing, even if we can't use it for compositing */
	intRect->width  = fixRect->x + fixRect->width  + 0xFFFF;																	/* ceil(right) */
	if (intRect->width >= fixRect->x)	{ intRect->width  = (intRect->width  >> 16) - (intRect->x = fixRect->x     >> 16); }	/* floor(x) */
	else 		/* overflow */			{ intRect->width  =  MAX_RECT_SIZE   >> 16;    intRect->x = MIN_RECT_COORD >> 16;  }
	intRect->height = fixRect->y + fixRect->height + 0xFFFF;																	/* ceil(bottom) */
	if (intRect->height >= fixRect->y)	{ intRect->height = (intRect->height >> 16) - (intRect->y = fixRect->y     >> 16); }	/* floor(y) */
	else 		/* overflow */			{ intRect->height = MAX_RECT_SIZE    >> 16;    intRect->y = MIN_RECT_COORD >> 16;  }
}


#define SIGMA_CUTOFF			2.5			/**< The sigma factor beyond which contributions are negligible. FskBlur.c uses 2.5 */
#define POLYGON_AURA_RADIUS		0x13800		/**< The radius of antialiasing aura around a polygon edge: 1.21875, from FskAATable.c, gFskGaussianNarrowPolygonFilter127_32 */
#define LINE_AURA_RADIUS		0x1C000		/**< The radius of antialiasing aura around a   line  edge: 1.75000, from FskAATable.c, gFskGaussRectConvLineFilter255_32     */

/****************************************************************************//**
 * ShadeMatteRect
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr ShadeMatteRect(FskCanvas2dContext ctx, FskFixedRectangle bbox) {
	FskCanvas2dContextState	*st					= FskCanvasGetStateFromContext(ctx);
	FskErr				err						= kFskErrNone;
	static const UInt8	compWithBBoxTable[]		= {		/* Which composition operations are restricted to the bounds of the source? */
		1,	/* kFskCompositePreSourceOver		*/
		1,	/* kFskCompositePreDestinationOver	*/
		0,	/* kFskCompositePreSourceIn			*/
		0,	/* kFskCompositePreDestinationIn	*/
		0,	/* kFskCompositePreSourceOut		*/
		1,	/* kFskCompositePreDestinationOut	*/
		1,	/* kFskCompositePreSourceAtop 		*/
		0,	/* kFskCompositePreDestinationAtop	*/
		1,	/* kFskCompositePreLighte			*/
		1	/* kFskCompositePreXor				*/
	};
	Boolean				compWithBBox	= (bbox != NULL) && ((st->globalCompositeOperation < kFskCompositePreSourceOver) || compWithBBoxTable[st->globalCompositeOperation - kFskCompositePreSourceOver]);
	Boolean				doShadow		= st->shadowColor.a && (st->shadowOffset.x || st->shadowOffset.y || st->shadowBlur);
	FskRectangleRecord	matteRect;

	/* If a bounding box was supplied and a shadow is to be applied, enlarge the bounding box appropriately to include the shadow */
	if (doShadow && bbox) {		/* Enlarge the bbox to account for the shadow */
		FskFixed d;
		if (st->shadowOffset.x) {
			d = (FskFixed)(st->shadowOffset.x * kFskFixedOne);
			if (d > 0) {
				d += 1;															/* The +1 acts as a "ceil(d)" */
				bbox->width += d;												/* Enlarge on the right. */
			}
			else {
				d -= 1;															/* The -1 acts as -ceil(abs(d)) */
				bbox->width -= d;												/* Enlarge on the right, adding abs(d). */
				bbox->x += d;													/* Subtracting abs(d) from x shifts the whole box to the left, leaving the right where it was originally. */
			}
		}
		if (st->shadowOffset.y) {
			d = (FskFixed)(st->shadowOffset.y * kFskFixedOne);
			if (d > 0) {
				d += 1;															/* The +1 acts as a "ceil(d)" */
				bbox->height += d;												/* Enlarge on the bottom. */
			}
			else {
				d -= 1;															/* The -1 acts as -ceil(abs(d)) */
				bbox->height -= d;												/* Enlarge on the bottom, adding abs(d). */
				bbox->y += d;													/* Subtracting abs(d) from y shifts the whole box to the top, leaving the bottom where it was originally. */
			}
		}
		if (st->shadowBlur) {
			d = (FskFixed)(SIGMA_CUTOFF * st->shadowBlur * kFskFixedOne) + 1;	/* The +1 acts as a ceil() */
			bbox->x -= d;														/* Enlarge by d all around */
			bbox->y -= d;
			d <<= 1;
			bbox->width  += d;
			bbox->height += d;
		}
	}

	if (compWithBBox || (doShadow && bbox))	FixedToIntBounds(bbox, &matteRect);	/* We can use the bounding box for shadowing, even if we can't use it for compositing */
	else									matteRect = st->clipRect;		/* If no bounding box was supplied, we use the clip rect as the matte rect */

	/* Apply the shadow - without the clip rect, because invisible things may cast a shadow */
	if (doShadow)
		BAIL_IF_ERR(err = FskShadow(&st->shadowOffset, st->shadowBlur, &st->shadowColor, &matteRect, 1, ctx->canvas->tmp));

	/* If we are using a graphics object's bounding box, intersect it with the clip rect */
	if (compWithBBox)	FskRectangleIntersect(&st->clipRect, &matteRect, &matteRect);
	else				matteRect = st->clipRect;	/* The shadow can always use the bbox whereas the compositor sometimes requires the whole clip region */

	/* Composite it in */
	err = FskMatteCompositeRect(st->globalCompositeOperation, st->globalAlpha, st->clipBM, ctx->canvas->tmp, &matteRect, ctx->canvas->bm, (const void*)(&matteRect));

bail:
	return err;
}


/*****************************************************************************//**
 * Enlarge the given Bounding Box of the geometry to accommodate the line width.
 * We assume that the incoming bounding box is valid, i.e. that width and height are non-negative.
 *	\param[in,out]	bbox		the bounding box.
 *	\param[in]		lineWidth	the width of the line used to stroke the geometry.
 ********************************************************************************/

static void EnlargeBoundingBox(FskFixedRectangle bbox, FskFixed lineWidth) {
	FskFixed d;																/* Antialiasing aura, plus half any frame width */
	if (lineWidth)	d = LINE_AURA_RADIUS + (lineWidth >> 1);				/* If drawing frames, with or without filling, the line's aura has a greater reach */
	else			d = POLYGON_AURA_RADIUS;								/* If not framing, the polygon aura suffices */

	if ((bbox->width += (d << 1)) < 0) {									/* Width overflow */
		bbox->width = MAX_RECT_SIZE; 										/* Max out box ... */
		bbox->x     = MIN_RECT_COORD;										/* ... centered around the origin */
	}
	else {
		if ((bbox->x >= 0) || ((MIN_S32 + d) <= bbox->x))					/* If bbox->x can be decremented without overflow ... */
			bbox->x -= d;													/* ... enlarge bbox on the left */
		else																/* Otherwise, ... */
			bbox->x = MIN_S32;												/* ... saturate */
	}

	if ((bbox->height += (d << 1)) < 0) {									/* Height overflow */
		bbox->height = MAX_RECT_SIZE; 										/* Max out box ... */
		bbox->y      = MIN_RECT_COORD;										/* ... centered around the origin */
	}
	else {
		if ((bbox->y >= 0) || ((MIN_S32 + d) <= bbox->y))					/* If bbox->y can be decremented without overflow ... */
			bbox->y -= d;													/* ... enlarge bbox on the left */
		else																/* Otherwise, ... */
			bbox->y = MIN_S32;												/* ... saturate */
	}
}


/****************************************************************************//**
 * Get the Joint Sharpness From the State.
 *	\param[in]	st	the Canvas context state.
 *	\return		the joint sharpness.
 ********************************************************************************/

static FskFixed
GetJointSharpnessFromState(const FskCanvas2dContextState *st)
{
	switch (st->lineJoin) {
		case kFskCanvas2dLineJoinRound:	return kFskLineJoinRound;
		case kFskCanvas2dLineJoinBevel:	return kFskLineJoinBevel;
		default:
		case kFskCanvas2dLineJoinMiter:	return st->miterLimit;
	}
}


/****************************************************************************//**
 * Get a Text Path That Fits the given maximum width.
 * Start with the specified font stretch, and condense it step by step until it fits.
 *	\param[in]	ctx			the Canvas context.
 *	\param[in]	uniChars	the Unicode characters of the string.
 *	\param[in]	x			the X-coordinate of the text anchor point.
 *	\param[in]	y			the Y-coordinate of the text anchor point.
 *	\param[in]	maxWidth	the maximum width of the string that can be tolerated.
 *	\param[in]	pPath		a place to store the resultant path.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

FskErr FskCanvasGetTextPathThatFits(FskCanvas2dContext ctx, const UInt16 *uniChars, double x, double y, double maxWidth, FskGrowablePath *pPath) {
	FskErr					err		= kFskErrNone;
	FskCanvas2dContextState	*st		= FskCanvasGetStateFromContext(ctx);
	FskFixedPoint2D			startPt, endPt;
	double					width;
	FskFontAttributes		myFont;

	*pPath	= NULL;
	startPt.x = FskRoundFloatToFixed(x);
	startPt.y = FskRoundFloatToFixed(y);
	for (myFont = st->font; myFont.stretch >= kFskFontStretchUltraCondensed; --myFont.stretch) {
		endPt = startPt;
		BAIL_IF_ERR(err = FskGrowablePathFromUnicodeStringNew(uniChars, FskUnicodeStrLen(uniChars), &myFont, &endPt, pPath));
		if ((maxWidth <= 0) || ((width = FskFixedToFloat(endPt.x - startPt.x)) <= maxWidth))
			break;
		FskGrowablePathDispose(*pPath);	/* Doesn't fit; try again with a more condensed font */
		*pPath = NULL;
	}
	BAIL_IF_NULL(*pPath, err, kFskErrTooMany);	/* Still doesn't fit */

	switch (st->textAlign) {
		case kFskCanvas2dTextAlignStart:
			if (1/*ScriptIsLeftToRight*/)	goto alignLeft;		// TODO: Accommodate right-to-left scripts
			else /*ScriptIsRightToLeft*/	goto alignRight;
		case kFskCanvas2dTextAlignEnd:
			if (1/*ScriptIsLeftToRight*/)	goto alignRight;	// TODO: Accommodate right-to-left scripts
			else /*ScriptIsRightToLeft*/	goto alignLeft;
		case kFskCanvas2dTextAlignCenter:
			err = FskGrowablePathOffset(*pPath, (startPt.x - endPt.x) >> 1, 0);
			break;
		case kFskCanvas2dTextAlignRight:
		alignRight:
			err = FskGrowablePathOffset(*pPath, startPt.x - endPt.x, 0);
			break;
		case kFskCanvas2dTextAlignLeft:
		alignLeft:
			break;
	}

	/* These adjustment should really be made from the font metrics */
	switch (st->textBaseline) {
		case kFskCanvas2dTextBaselineAlphabetic:	/* default */
		case kFskCanvas2dTextBaselineIdeographic:	/* Used by Chinese, Japanese, Korean, and Vietnamese Chu Nom scripts */
			break;
		case kFskCanvas2dTextBaselineTop:			/* Shift down by em height */
		case kFskCanvas2dTextBaselineHanging:		/* Used by the Indic scripts Devanagari, Gurmukhi and Bengali */
			err = FskGrowablePathOffset(*pPath, 0, FskRoundFloatToFixed(st->font.size*.75));		/* em height is the font size */
			break;
		case kFskCanvas2dTextBaselineBottom:		/* Shift up by descender height */
			err = FskGrowablePathOffset(*pPath, 0, FskRoundFloatToFixed(st->font.size*-.25));	/* Hack: assume descender height is equal to 1/2 the ex height */
			break;
		case kFskCanvas2dTextBaselineMiddle:		/* Shift down by 1/2 x height */
			err = FskGrowablePathOffset(*pPath, 0, FskRoundFloatToFixed(st->font.size*.25));	/* Hack: ex height assumed as 1/2 em height */
			break;
	}

bail:
	return err;
}


/****************************************************************************//**
 * Intersect one Matte with another.
 * This could be made more efficient by only looking in the bounds.
 *	\param[in]	src	the source matte.
 *	\param[in]	srcRect	the subrrect of the src to intersect with the dst. NULL implied the whole src.
 *	\param[out]	dst	the destination matte.
 *	\param[in]	dstRect	the subrrect of the dst to be intersected by the srcRect of the src. NULL implied the whole dst.
 ********************************************************************************/

static void IntersectMattes(FskConstBitmap src, FskConstRectangle srcRect, FskBitmap dst, FskRectangle dstRect) {
	const UInt8	*s;
	UInt8		*d;
	SInt32		sBump, dBump, x, y, w, h;

	FskBitmapReadBegin((FskBitmap)src, (const void**)(const void*)&s, &sBump, NULL);
	FskBitmapWriteBegin(          dst,             (void**)(void*)&d, &dBump, NULL);
	if (srcRect)
		FskRectangleIntersect(srcRect, dstRect, dstRect);
	s += dstRect->y * sBump + dstRect->x;
	d += dstRect->y * dBump + dstRect->x;
	sBump -= dstRect->width;
	dBump -= dstRect->width;
	if (srcRect) {	/* If we have a good idea of the src bounds, use that. */
		for (h = dstRect->height; h--; s += sBump, d += dBump)
			for (w = dstRect->width; w--; ++s, ++d)
				*d = FskAlphaMul(*s, *d);
	}
	else {			/* Otherwise, compute it. */
		SInt32	x1 = dstRect->x;
		SInt32	y1 = dstRect->y;
		SInt32	x0 = x1 + dstRect->width  - 1;
		SInt32	y0 = y1 + dstRect->height - 1;
		for (h = dstRect->height, y = dstRect->y; h--; ++y, s += sBump, d += dBump) {
			for (w = dstRect->width, x = dstRect->x; w--; ++x, ++s, ++d) {
				if ((*d = FskAlphaMul(*s, *d)) != 0) {
					if (x0 > x)
						x0 = x;
					if (x1 < x)
						x1 = x;
					if (y0 > y)
						y0 = y;
					if (y1 < y)
						y1 = y;
				}
			}
		}
		FskRectangleSet(dstRect, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
	}

	FskBitmapWriteEnd(dst);
	FskBitmapReadEnd((FskBitmap)src);
}


#ifdef UNUSED
/****************************************************************************//**
 * Double3x2ToFloat3x3Matrix
 ********************************************************************************/

static void Double3x2ToFloat3x3Matrix(const double *d, float *f) {
	int i;
	for (i = 3; i--;) {
		*f++ = (float)(*d++);
		*f++ = (float)(*d++);
		*f++ = i ? 0.f : 1.f;
	}
}
#endif /* UNUSED */


/****************************************************************************//**
 * Convert a 3x2 Fixed Point Matrix To a 3x3 Single Precision Floating Point Matrix.
 *	\param[in]	x	the fixed point matrix.
 *	\param[out]	y	the floating-point matrix.
 ********************************************************************************/

static void Fixed3x2ToFloat3x3Matrix(const FskFixed *x, float *f) {
	int i;
	for (i = 3; i--;) {
		*f++ = (float)FskFixedToFloat(*x++);
		*f++ = (float)FskFixedToFloat(*x++);
		*f++ = i ? 0.f : 1.f;
	}
}


/****************************************************************************//**
 * Draw a Transformed Bitmap.
 * Stretch (sx,sy,sw,sh) to (dx,dy,dw,dh).
 * TODO: It might be better to render a rectangle filled with a texture color source.
 *	\param[in]	st		the Canvas context state.
 *	\param[in]	src		the source bitmap.
 *	\param[in]	sx		the left edge of the src.
 *	\param[in]	sy		the top  edge of the src.
 *	\param[in]	sw		the  width    of the src.
 *	\param[in]	sh		the  height   of the src.
 *	\param[in]	dst		the destination bitmap.
 *	\param[in]	dx		the left edge of the dst.
 *	\param[in]	dy		the top  edge of the dst.
 *	\param[in]	dw		the  width    of the dst.
 *	\param[in]	dh		the  height   of the dst.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

static FskErr DrawTransformedBitmap(const FskCanvas2dContextState *st,
									FskConstBitmap src, double sx, double sy, double sw, double sh,
									FskBitmap      dst, double dx, double dy, double dw, double dh) {
	void		*dp		= NULL;
	const void	*sp		= NULL;
	const float	zeroish	= 1.f / 16384.f;
	float		S[3][3], T[3][3], U[3][3], srcPts[4][2];
	Boolean		isTrue;
	FskErr		err;
	UInt32		mode;

	/* Get pixel pointers */
	BAIL_IF_ERR(err = FskBitmapReadBegin((FskBitmap)src, &sp, NULL, NULL));
	BAIL_IF_ERR(err = FskBitmapWriteBegin(          dst, &dp, NULL, NULL));

	/* Get mode */
	#if SRC_32A16RGB565LE
		if (src->pixelFormat == FskName2(kFskBitmapFormat,fsk32A16RGB565SEKindFormat))
			FskConvert32A16BitmapToDefault32((FskBitmap)src);		/* Premultiplied 32A16RGB565 looks horrible: prevent it */
	#endif /* SRC_32A16RGB565LE */
	FskStraightAlphaToPremultipliedAlpha((FskBitmap)(src), NULL);    // TODO: We shouldn't modify a const bitmap.
	if ((FskBitmapGetHasAlpha(src, &isTrue), isTrue)) {
		mode = kFskGraphicsModeAlpha | kFskGraphicsModeAffine;
		if ((FskBitmapGetAlphaIsPremultiplied(src, &isTrue), isTrue))
			mode |= kFskSrcIsPremul;
	}
	else {
		mode = kFskGraphicsModeCopy | kFskGraphicsModeAffine;
	}
	if (st->quality)
		mode |= kFskGraphicsModeBilinear;

	/* Get transform */
	S[0][0] = (float)(dw - 1.f) / (float)(sw - 1.f);	S[1][0] = 0.f;	S[2][0] = (float)(dx - sx * S[0][0]);
	S[1][1] = (float)(dh - 1.f) / (float)(sh - 1.f);	S[0][1] = 0.f;	S[2][1] = (float)(dy - sy * S[1][1]);
	S[0][2] = 0.f;										S[1][2] = 0.f;	S[2][2] = 1.f;
	Fixed3x2ToFloat3x3Matrix(st->fixedTransform.M[0], T[0]);
	FskSLinearTransform(S[0], T[0], U[0], 3, 3, 3);

	/* Get points */
	srcPts[0][0] = srcPts[1][0] = (float)(sx)				- zeroish;
	srcPts[2][0] = srcPts[3][0] = (float)(sx + sw - 1.)	+ zeroish;
	srcPts[0][1] = srcPts[3][1] = (float)(sy)				- zeroish;
	srcPts[1][1] = srcPts[2][1] = (float)(sy + sh - 1.)	+ zeroish;

	err = FskProjectImage(	sp, src->pixelFormat, src->rowBytes, src->bounds.width, src->bounds.height, (const float(*)[3])U, 4, (const float(*)[2])srcPts, mode, NULL,
							dp, dst->pixelFormat, dst->rowBytes, dst->bounds.width, dst->bounds.height, 0, NULL);

bail:
	if (sp)	FskBitmapReadEnd((FskBitmap)src);
	if (dp)	FskBitmapWriteEnd(dst);
	return err;
}


/****************************************************************************//**
 * Determine whether the State Matrix Is the Identity.
 *	\param[in]	the Canvas context state.
 *	\return		true,	if the state matrix is the identity.
 *	\return		false,	otherwise.
 ********************************************************************************/

Boolean FskCanvasStateMatrixIsIdentity(FskCanvas2dContextState *st) {
	return	0 == ((st->fixedTransform.M[0][0] - kFskFixedOne)	|
				  (st->fixedTransform.M[1][1] - kFskFixedOne)	|
				  (st->fixedTransform.M[0][1])					|
				  (st->fixedTransform.M[1][0])					|
				  (st->fixedTransform.M[2][0])					|
				  (st->fixedTransform.M[2][1])					);
}


/****************************************************************************//**
 * Convert the ImageData Format.
 *	\param[in,out]	id	the image data.
 *	\param[in]		srcFmt	the initial format.
 *	\param[out]		dstFmt	the desired format.
 *	\return		kFskErrNone	if the operation was completed successfully.
 ********************************************************************************/

FskErr FskCanvasConvertImageDataFormat(FskCanvas2dImageData id, FskBitmapFormatEnum srcFmt, FskBitmapFormatEnum dstFmt)
{
	FskErr		err		= kFskErrNone;
	UInt32		*p		= (UInt32*)(id->data.bytes);
	UInt32		n		= id->data.length / sizeof(UInt32);
	UInt32		pix;

	if (srcFmt == dstFmt)
		return kFskErrNone;

	switch (srcFmt) {
		case FskName2(kFskBitmapFormat,fsk32ARGBFormatKind): switch (dstFmt) {
			case FskName2(kFskBitmapFormat,fsk32BGRAFormatKind): for (; n--; ++p) { pix=*p; fskConvert32ARGB32BGRA(pix); *p=pix; } break;
			case FskName2(kFskBitmapFormat,fsk32RGBAFormatKind): for (; n--; ++p) { pix=*p; fskConvert32ARGB32RGBA(pix); *p=pix; } break;
			case FskName2(kFskBitmapFormat,fsk32ABGRFormatKind): for (; n--; ++p) { pix=*p; fskConvert32ARGB32ABGR(pix); *p=pix; } break;
			default: err = kFskErrUnsupportedPixelType;
		} break;
		case FskName2(kFskBitmapFormat,fsk32BGRAFormatKind): switch (dstFmt) {
			case FskName2(kFskBitmapFormat,fsk32ARGBFormatKind): for (; n--; ++p) { pix=*p; fskConvert32BGRA32ARGB(pix); *p=pix; } break;
			case FskName2(kFskBitmapFormat,fsk32RGBAFormatKind): for (; n--; ++p) { pix=*p; fskConvert32BGRA32RGBA(pix); *p=pix; } break;
			case FskName2(kFskBitmapFormat,fsk32ABGRFormatKind): for (; n--; ++p) { pix=*p; fskConvert32BGRA32ABGR(pix); *p=pix; } break;
			default: err = kFskErrUnsupportedPixelType;
		} break;
		case FskName2(kFskBitmapFormat,fsk32RGBAFormatKind): switch (dstFmt) {
			case FskName2(kFskBitmapFormat,fsk32ARGBFormatKind): for (; n--; ++p) { pix=*p; fskConvert32RGBA32ARGB(pix); *p=pix; } break;
			case FskName2(kFskBitmapFormat,fsk32BGRAFormatKind): for (; n--; ++p) { pix=*p; fskConvert32RGBA32BGRA(pix); *p=pix; } break;
			case FskName2(kFskBitmapFormat,fsk32ABGRFormatKind): for (; n--; ++p) { pix=*p; fskConvert32RGBA32ABGR(pix); *p=pix; } break;
			default: err = kFskErrUnsupportedPixelType;
		} break;
		case FskName2(kFskBitmapFormat,fsk32ABGRFormatKind): switch (dstFmt) {
			case FskName2(kFskBitmapFormat,fsk32ARGBFormatKind): for (; n--; ++p) { pix=*p; fskConvert32ABGR32ARGB(pix); *p=pix; } break;
			case FskName2(kFskBitmapFormat,fsk32BGRAFormatKind): for (; n--; ++p) { pix=*p; fskConvert32ABGR32BGRA(pix); *p=pix; } break;
			case FskName2(kFskBitmapFormat,fsk32RGBAFormatKind): for (; n--; ++p) { pix=*p; fskConvert32ABGR32RGBA(pix); *p=pix; } break;
			default: err = kFskErrUnsupportedPixelType;
		} break;
		default: err = kFskErrUnsupportedPixelType;
	}
	return err;
}


/*******************************************************************************
 *******************************************************************************
 *******************************************************************************
 ****									API 								****
 *******************************************************************************
 *******************************************************************************
 *******************************************************************************/


/********************************************************************************
 * FskCanvasNew
 ********************************************************************************/

#define kInitialStackSize 4			/**< The initial size of the Canvas context state stack. */

/* Canvas constructor.  Use pixelFormat == 0 for the default pixel format */
FskErr
FskCanvasNew(UInt32 width, UInt32 height, FskBitmapFormatEnum pixelFormat, FskCanvas *pCnv)
{
	FskErr		err		= kFskErrNone;
	FskCanvas	cnv		= NULL;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvasNew(w=%u h=%u fmt=%s)", (unsigned)width, (unsigned)height, FskBitmapFormatName(pixelFormat));
	#endif /* LOG_PARAMETERS */

	BAIL_IF_NULL(pCnv, err, kFskErrInvalidParameter);
	*pCnv = NULL;																/* Set canvas to NULL in case we fail */

	BAIL_IF_ERR(err = FskMemPtrNewClear(sizeof(FskCanvasRecord), &cnv));
	if (width > 0 && height > 0) {
		BAIL_IF_ERR(err = FskBitmapNew(width, height, pixelFormat, &cnv->bm));
		BAIL_IF_ERR(err = FskBitmapSetHasAlpha(cnv->bm, true));					/* Alpha things don't happen unless this is set */
		BAIL_IF_ERR(err = FskBitmapSetAlphaIsPremultiplied(cnv->bm, true));		/* Alpha should be premultiplied */
		cnv->notMyBitmap = false;
		ClearBufferToValue(cnv->bm, 0);
	}
	else {
		cnv->bm = NULL;
	}

	BAIL_IF_ERR(err = InitCanvasContext(&cnv->ctx, cnv, kInitialStackSize));	/* This relies on having a bitmap */
	*pCnv = cnv;																/* Allocation successful. OK to return canvas pointer. */

bail:
	if (err)	FskCanvasDispose(cnv);
	return err;
}


/********************************************************************************
 * FskCanvasNewFromBitmap
 ********************************************************************************/

FskErr
FskCanvasNewFromBitmap(FskBitmap bm, FskCanvas *pCnv)
{
	FskErr		err		= kFskErrNone;
	FskCanvas	cnv		= NULL;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvasNewFromBitmap(bm=%p)", bm);
		LogBitmap(bm, "bm");
	#endif /* LOG_PARAMETERS */

	BAIL_IF_FALSE(bm && pCnv, err, kFskErrInvalidParameter);
	*pCnv = NULL;																/* Set canvas to NULL in case we fail */

	BAIL_IF_ERR(err = FskMemPtrNewClear(sizeof(FskCanvasRecord), &cnv));
	cnv->bm = bm;
	cnv->notMyBitmap = true;
	BAIL_IF_ERR(err = InitCanvasContext(&cnv->ctx, cnv, kInitialStackSize));	/* This relies on having a bitmap */
	*pCnv = cnv;																/* Allocation successful. OK to return canvas pointer. */

bail:
	if (err)	FskCanvasDispose(cnv);
	return err;
}


/********************************************************************************
 * FskCanvasDispose
 ********************************************************************************/

void
FskCanvasDispose(FskCanvas cnv)
{
	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvasDispose(cnv=%p)", cnv);
	#endif /* LOG_PARAMETERS */

	if (!cnv)
		return;
	if (cnv->bm && !cnv->notMyBitmap)
		FskBitmapDispose(cnv->bm);
    if (cnv->tmp)
        FskBitmapDispose(cnv->tmp);
	CleanupCanvasContext(&cnv->ctx);
	FskMemPtrDispose(cnv);
}


/********************************************************************************
 * FskSetCanvasBitmap
 ********************************************************************************/

FskErr
FskSetCanvasBitmap(FskCanvas cnv, FskBitmap bm)
{
	FskErr	err		= kFskErrNone;

	#if defined(LOG_PARAMETERS)
		LOGD("FskSetCanvasBitmap(cnv=%p bm=%p)", cnv, bm);
	#endif /* LOG_PARAMETERS */

	if (!cnv->notMyBitmap)
		FskBitmapDispose(cnv->bm);

	cnv->bm = bm;
	cnv->notMyBitmap = true;
	if (bm && bm->depth == 32) {
		FskBitmapSetHasAlpha(bm, true);
		FskBitmapSetAlphaIsPremultiplied(bm, true);
	}
	ResetCanvasContext(&cnv->ctx);

	//@@ need to update clipRect across the state

	return err;
}



/********************************************************************************
 * ACCESSORS
 ********************************************************************************/

FskCanvas2dContext	FskCanvasGet2dContext(FskCanvas cnv) {
						return cnv	? &cnv->ctx
									: NULL;
					}
FskConstBitmap		FskGetCanvasBitmap(FskCanvas cnv) {
						return cnv	? cnv->bm
									: NULL;
					}
FskCanvas			FskCanvas2dGetCanvas(FskCanvas2dContext ctx) {
						return ctx	? ctx->canvas
									: NULL;
					}

UInt32				FskCanvas2dGetGlobalCompositeOperation(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? st->globalCompositeOperation
									: -1;
					}
double				FskCanvas2dGetGlobalAlpha(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? (1./255.) * st->globalAlpha
									: NaN64();
					}
UInt32				FskCanvas2dGetQuality(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? st->quality
									: -1;
					}
UInt32				FskCanvas2dGetLineCap(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? st->lineCap
									: -1;
					}
UInt32				FskCanvas2dGetLineJoin(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? st->lineJoin
									: -1;
					}
double				FskCanvas2dGetLineWidth(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? FskFixedToFloat(st->lineWidth)
									: NaN64();
					}
double				FskCanvas2dGetMiterLimit(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? FskFixedToFloat(st->miterLimit)
									: NaN64();
					}
double				FskCanvas2dGetShadowOffsetX(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? FskIntToFloat(st->shadowOffset.x)
									: NaN64();
					}
double				FskCanvas2dGetShadowOffsetY(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? FskIntToFloat(st->shadowOffset.y)
									: NaN64();
					}
double				FskCanvas2dGetShadowBlur(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? (double)st->shadowBlur
									: NaN64();
					}
FskConstColorRGBA	FskCanvas2dGetShadowColor(FskConstCanvas2dContext ctx) {
						const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
						return st	? &st->shadowColor
									: NULL;
					}
const FskColorSource*	FskCanvas2dGetStrokeStyle(FskConstCanvas2dContext ctx) {
							const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
							return st	? &st->strokeStyle.csu.so
										: NULL;
						}
const FskColorSource*	FskCanvas2dGetFillStyle(FskConstCanvas2dContext ctx) {
							const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
							return st	? &st->fillStyle.csu.so
										: NULL;
						}
UInt32	FskCanvas2dGetTextAlignment(FskConstCanvas2dContext ctx) {
			const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
			return st	? st->textAlign
						: -1;
		}
UInt32	FskCanvas2dGetTextBaseline(FskConstCanvas2dContext ctx) {
			const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
			return st	? st->textBaseline
						: -1;
		}

void	FskCanvas2dSetGlobalCompositeOperation(FskCanvas2dContext ctx, UInt32 op) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st)
				st->globalCompositeOperation = (UInt8)op;
		}
void	FskCanvas2dSetGlobalAlpha(FskCanvas2dContext ctx, double alpha) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && alpha >= 0. && alpha <= 1.)
					st->globalAlpha = (UInt8)(alpha * 255. + .5);
		}
void	FskCanvas2dSetQuality(FskCanvas2dContext ctx, UInt32 quality) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st)
				st->quality = (UInt8)quality;
		}
void	FskCanvas2dSetLineCap(FskCanvas2dContext ctx, UInt32 lineCap) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st)
				st->lineCap = (UInt8)lineCap;
		}
void	FskCanvas2dSetLineJoin(FskCanvas2dContext ctx, UInt32 lineJoin) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st)
				st->lineJoin = (UInt8)lineJoin;
		}
void	FskCanvas2dSetLineWidth(FskCanvas2dContext ctx, double w) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && IsFiniteNonnegative(w))
				st->lineWidth = FskRoundFloatToFixed(w);
		}
void	FskCanvas2dSetMiterLimit(FskCanvas2dContext ctx, double miterLimit) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && IsFiniteNonnegative(miterLimit))
				st->miterLimit = FskRoundFloatToFixed(miterLimit);
		}
void	FskCanvas2dSetShadowOffsetX(FskCanvas2dContext ctx, double ox) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && IsFinite(ox))
				st->shadowOffset.x = FskRoundFloatToInt(ox);
		}
void	FskCanvas2dSetShadowOffsetY(FskCanvas2dContext ctx, double oy) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && IsFinite(oy))
				st->shadowOffset.y = FskRoundFloatToInt(oy);
		}
void	FskCanvas2dSetShadowBlur(FskCanvas2dContext ctx, double blur) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && IsFinite(blur) && blur >= 0. && blur < 500.)
				st->shadowBlur = (float)blur;
		}
void	FskCanvas2dSetShadowColor(FskCanvas2dContext ctx, FskConstColorRGBA color) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && color)
				st->shadowColor = *color;
		}
void	FskCanvas2dSetTextAlignment(FskCanvas2dContext ctx, UInt32 alignment) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st)
				st->textAlign = (UInt8)alignment;
		}
void	FskCanvas2dSetTextBaseline(FskCanvas2dContext ctx, UInt32 baseline) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st)
				st->textBaseline = (UInt8)baseline;
		}

void	FskCanvas2dSetStrokeStyle(FskCanvas2dContext ctx, const FskColorSource *cs) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && cs)
				SetCanvas2dColorSource(cs, &st->strokeStyle);
		}
void	FskCanvas2dSetFillStyle(FskCanvas2dContext ctx, const FskColorSource *cs) {
			FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
			if (st && cs)
				SetCanvas2dColorSource(cs, &st->fillStyle);
		}

void	FskCanvas2dSetStrokeStyleColor(FskCanvas2dContext ctx, FskConstColorRGBA color) {
			FskCanvas2dColorSource *so = GetStrokeColorSourcePtr(ctx);
			if (so && color)
				SetCanvas2dColorSourceColor(so, color);
		}
void	FskCanvas2dSetFillStyleColor(FskCanvas2dContext ctx, FskConstColorRGBA color) {
			FskCanvas2dColorSource *so = GetFillColorSourcePtr(ctx);
			if (so && color)
				SetCanvas2dColorSourceColor(so, color);
		}

FskErr FskCanvas2dSetFillStyleLinearGradient(FskCanvas2dContext ctx, double x0, double y0, double x1, double y1, UInt32 numStops, const FskCanvas2dGradientStop *stops) {
			FskCanvas2dColorSource *so = GetFillColorSourcePtr(ctx);
			return so	? SetCanvas2dColorSourceLinearGradient(so, x0, y0, x1, y1, numStops, stops)
						: kFskErrInvalidParameter;
		}
FskErr FskCanvas2dSetStrokeStyleLinearGradient(FskCanvas2dContext ctx, double x0, double y0, double x1, double y1, UInt32 numStops, const FskCanvas2dGradientStop *stops) {
			FskCanvas2dColorSource *so = GetStrokeColorSourcePtr(ctx);
			return so	? SetCanvas2dColorSourceLinearGradient(so, x0, y0, x1, y1, numStops, stops)
						: kFskErrInvalidParameter;
		}

FskErr FskCanvas2dSetFillStyleRadialGradient(FskCanvas2dContext ctx, double x0, double y0, double r0, double x1, double y1, double r1, UInt32 numStops, const FskCanvas2dGradientStop *stops) {
			FskCanvas2dColorSource *so = GetFillColorSourcePtr(ctx);
			return so	? SetCanvas2dColorSourceRadialGradient(so, x0, y0, r0, x1, y1, r1, numStops, stops)
						: kFskErrInvalidParameter;
		}
FskErr FskCanvas2dSetStrokeStyleRadialGradient(FskCanvas2dContext ctx, double x0, double y0, double r0, double x1, double y1, double r1, UInt32 numStops, const FskCanvas2dGradientStop *stops) {
			FskCanvas2dColorSource *so = GetStrokeColorSourcePtr(ctx);
			return so	? SetCanvas2dColorSourceRadialGradient(so, x0, y0, r0, x1, y1, r1, numStops, stops)
						: kFskErrInvalidParameter;
		}

FskErr FskCanvas2dSetFillStylePattern(FskCanvas2dContext ctx, UInt32 rep, FskConstBitmap texture) {
			FskCanvas2dColorSource *so = GetFillColorSourcePtr(ctx);
			return so	? SetCanvas2dColorSourcePattern(so, rep, texture)
						: kFskErrInvalidParameter;
		}
FskErr FskCanvas2dSetStrokeStylePattern(FskCanvas2dContext ctx, UInt32 rep, FskConstBitmap texture) {
			FskCanvas2dColorSource *so = GetStrokeColorSourcePtr(ctx);
			return so	? SetCanvas2dColorSourcePattern(so, rep, texture)
						: kFskErrInvalidParameter;
		}

const struct FskFontAttributes*	FskCanvas2dGetFont(FskCanvas2dContext ctx) {
			const FskCanvas2dContextState *st = FskCanvasGetConstStateFromContext(ctx);
			return st	? &st->font
						: NULL;
		}


/********************************************************************************
 * FskCanvas2dSetFont
 ********************************************************************************/

FskErr FskCanvas2dSetFont(FskCanvas2dContext ctx, const struct FskFontAttributes *font)
{
	FskErr					err		= kFskErrNone;
	FskCanvas2dContextState	*st		= FskCanvasGetStateFromContext(ctx);

	if (!st || !font)
		return kFskErrInvalidParameter;

	st->font = *font;
	if (strlen(font->family) >= sizeof(st->fontFamily))
		err = kFskErrBufferOverflow;
	memcpy(st->fontFamily, st->font.family, sizeof(st->fontFamily)); st->fontFamily[sizeof(st->fontFamily)-1] = 0;
	st->font.family = st->fontFamily;
	return err;
}


/********************************************************************************
 * FskCanvasSave
 ********************************************************************************/

FskErr
FskCanvas2dSave(FskCanvas2dContext ctx)
{
	FskErr	err		= kFskErrNone;
	FskCanvas2dContextState *st;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dSave(ctx=%p) when level=%u", ctx, (unsigned)FskGrowableArrayGetItemCount(ctx->state));
	#endif /* LOG_PARAMETERS */

	BAIL_IF_NULL(ctx, err, kFskErrInvalidParameter);
	BAIL_IF_ERR(err = FskGrowableArrayGetPointerToNewEndItem(ctx->state, (void**)(void*)(&st)));
	st[0] = st[-1];
	if (st[-1].clipBM)
		BAIL_IF_ERR(err = FskBitmapClone(st[-1].clipBM, &st[0].clipBM));
	FixUpCanvasContextPointers(ctx);

bail:
	return err ;
}


/********************************************************************************
 * FskCanvas2dRestore
 ********************************************************************************/

FskErr
FskCanvas2dRestore(FskCanvas2dContext ctx)
{
	FskErr	err = kFskErrNone;
	UInt32	n;
	FskCanvas2dContextState *st;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dRestore(ctx=%p) when level=%u", ctx, (unsigned)FskGrowableArrayGetItemCount(ctx->state));
	#endif /* LOG_PARAMETERS */

	BAIL_IF_FALSE(ctx, err, kFskErrInvalidParameter);
	BAIL_IF_FALSE((n = FskGrowableArrayGetItemCount(ctx->state)) > 1, err, kFskErrEmpty);
	BAIL_IF_FALSE(st = FskCanvasGetStateFromContext(ctx), err, kFskErrBadState);
	CleanupCanvasState(st);
	FskGrowableArraySetItemCount(ctx->state, n - 1);
	FixUpCanvasContextPointers(ctx);

bail:
	return err;
}


/********************************************************************************
 * FskCanvasClear
 ********************************************************************************/

void
FskCanvasClear(FskCanvas cnv, UInt8 r, UInt8 g, UInt8 b, UInt8 a)
{
	FskColorRGBARecord color;
	FskColorRGBASet(&color, r, g, b, a);
	if (cnv && cnv->bm)
		FskRectangleFill(cnv->bm, &cnv->bm->bounds, &color, 0, NULL);
}


/********************************************************************************
 * FskBitmapToDataURL
 ********************************************************************************/

FskErr
FskBitmapToDataURL(FskConstBitmap bm, const char *type, float quality, char **dataURL)
{
	FskErr				err			= kFskErrNone;
	FskImageCompress	ic			= NULL;
	void				*cmprImg	= NULL;
	char				*b64		= NULL;
	static const char	dataStr[]	= "data:";
	static const char	baseStr[]	= ";base64,";
	UInt32				cmprSize, dur, cto, frt, b64Size, headerSize, urlSize;
	char				*url;
	Boolean				isJPEG;
	FskMediaPropertyValueRecord		mpvr;

	BAIL_IF_FALSE(bm && dataURL, err, kFskErrInvalidParameter);
	*dataURL = NULL;

	if (type == NULL)
		type = "image/png";

	if ((isJPEG = ( 0==strcmp(type, "image/jpeg")))	||
					0==strcmp(type, "image/png")	||
					0==strcmp(type, "image/bmp")
	) {
		/* Compress image */
		BAIL_IF_ERR(err = FskImageCompressNew(&ic, bm->pixelFormat, type, bm->bounds.width, bm->bounds.height));
		if (isJPEG && quality > 0) {
			mpvr.type = kFskMediaPropertyQuality;
			mpvr.value.number = quality;
			BAIL_IF_ERR(err = FskImageCompressSetProperty(ic, kFskMediaPropertyQuality, &mpvr));
		}
		BAIL_IF_ERR(err = FskImageCompressFrame(ic, (FskBitmap)bm, &cmprImg, &cmprSize, &dur, &cto, &frt, NULL, NULL));
		BAIL_IF_ERR(err = FskImageCompressFlush(ic));
		FskImageCompressDispose(ic); ic = NULL;

		/* Base64 encode */
		FskStrB64Encode((char*)cmprImg, cmprSize, &b64, &b64Size, true);	/* size includes a trailing \0 */
		BAIL_IF_NULL(b64, err, kFskErrMemFull);
		FskMemPtrDisposeAt(&cmprImg);

		/* Assemble header and payload into URL */
		headerSize = strlen(dataStr) + strlen(type) + strlen(baseStr);
		urlSize = headerSize + b64Size;										/* b64Size contains a trailing \0 */
		BAIL_IF_ERR(err = FskMemPtrNew(urlSize, dataURL));
		url = *dataURL;
		snprintf(url, headerSize+1, "%s%s%s", dataStr, type, baseStr);

		FskMemCopy(url + headerSize, b64, b64Size);							/* base-64-encoded image, including the trailing NULL */
	}

	else if (0==strcmp(type, "image/svg+xml")) {
		return kFskErrUnimplemented;
	}
	else {
		return kFskErrUnimplemented;
	}

bail:
	FskImageCompressDispose(ic);
	FskMemPtrDispose(cmprImg);
	FskMemPtrDispose(b64);
	if (err)		FskMemPtrDisposeAt(dataURL);
	return err;
}


/********************************************************************************
 * FskCanvasToDataURL
 ********************************************************************************/

FskErr
FskCanvasToDataURL(FskCanvas cnv, const char *type, float quality, char **dataURL)
{
	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvasToDataURL(cnv=%p type=\"%s\" quality=%g)", cnv, type, quality);
	#endif /* LOG_PARAMETERS */

	return FskBitmapToDataURL(cnv->bm, type, quality, dataURL);
}



/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *****								TRANSFORMS								*****
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/


/********************************************************************************
 * UpdateStateFixedTransform
 * This concatenates the device transform and converts to fixed point.
 ********************************************************************************/

static void UpdateStateFixedTransform(FskConstCanvas2dContext ctx, FskCanvas2dContextState *st) {
	FskCanvasMatrix3x2d T;
	MultiplyMatrices3x2(&st->transform, &ctx->deviceTransform, &T);	/* Apply the device transform. */
	SetFixedFromDoubleMatrix(&st->fixedTransform, &T);				/* Create a fixed-point version for the renderer. */
}


/********************************************************************************
 * FskCanvas2dSetDeviceTransform
 ********************************************************************************/

void
FskCanvas2dSetDeviceTransform(FskCanvas2dContext ctx, double a, double b, double c, double d, double e, double f)
{
	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dSetDeviceTransform(ctx=%p [[%g %g][%g %g][%g %g]])", ctx, a, b, c, d, e, f);
	#endif /* LOG_PARAMETERS */

	if (ctx) {
		UInt32 i;
		FskCanvas2dContextState *st;
		SetMatrix3x2(&ctx->deviceTransform, a, b, c, d, e, f);
		for (i = FskGrowableArrayGetItemCount(ctx->state); i--;)
			if (FskGrowableArrayGetPointerToItem(ctx->state, i, (void**)(void*)(&st)) == kFskErrNone)
				UpdateStateFixedTransform(ctx, st);
	}
}


/********************************************************************************
 * FskCanvas2dGetDeviceTransform
 ********************************************************************************/

void
FskCanvas2dGetDeviceTransform(FskCanvas2dContext ctx, double transform[6])
{
	if (ctx && transform)
		*((FskCanvasMatrix3x2d*)transform) = ctx->deviceTransform;
}


/********************************************************************************
 * FskCanvas2dSetTransform
 ********************************************************************************/

void
FskCanvas2dSetTransform(FskCanvas2dContext ctx, double a, double b, double c, double d, double e, double f) {
	FskCanvas2dContextState *st;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dSetTransform(ctx=%p [[%g %g][%g %g][%g %g]])", ctx, a, b, c, d, e, f);
	#endif /* LOG_PARAMETERS */

	if ((st = FskCanvasGetStateFromContext(ctx)) != NULL) {
		SetMatrix3x2(&st->transform, a, b, c, d, e, f);	/* Set the current transform. */
		UpdateStateFixedTransform(ctx, st);				/* Concatenate the device transform and convert to fixed point */
	}
}


/********************************************************************************
 * FskCanvas2dGetTransform
 ********************************************************************************/

void
FskCanvas2dGetTransform(FskCanvas2dContext ctx, double transform[6])
{
	FskCanvas2dContextState	*st = FskCanvasGetStateFromContext(ctx);
	if (st && transform)
		*((FskCanvasMatrix3x2d*)transform) = st->transform;
}


/********************************************************************************
 * FskCanvas2dTransform
 ********************************************************************************/

void
FskCanvas2dTransform(FskCanvas2dContext ctx, double a, double b, double c, double d, double e, double f)
{
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
	FskCanvasMatrix3x2d U;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dTransform(ctx=%p [[%g %g][%g %g][%g %g]])", ctx, a, b, c, d, e, f);
	#endif /* LOG_PARAMETERS */

	if (!st)
		return;

	/* Concatenate the given transformation matrix to the current transform. */
	U.M[0][0] = a * st->transform.M[0][0] + b * st->transform.M[1][0];							U.M[0][1] = a * st->transform.M[0][1] + b * st->transform.M[1][1];
	U.M[1][0] = c * st->transform.M[0][0] + d * st->transform.M[1][0];							U.M[1][1] = c * st->transform.M[0][1] + d * st->transform.M[1][1];
	U.M[2][0] = e * st->transform.M[0][0] + f * st->transform.M[1][0] + st->transform.M[2][0];	U.M[2][1] = e * st->transform.M[0][1] + f * st->transform.M[1][1] + st->transform.M[2][1];
	st->transform = U;		/* Update current transform */

	/* Apply the device transform. */
	MultiplyMatrices3x2(&st->transform, &ctx->deviceTransform, &U);

	/* Create a fixed-point version for the renderer. */
	SetFixedFromDoubleMatrix(&st->fixedTransform, &U);
}


/********************************************************************************
 * FskCanvas2d Rotate, Scale, Translate
 ********************************************************************************/

void FskCanvas2dRotate(   FskCanvas2dContext ctx, double a) { double c = cos(a), s = sin(a);	FskCanvas2dTransform(ctx,	c,	s,	-s,	c,	0.,	0.);	}
void FskCanvas2dScale(    FskCanvas2dContext ctx, double x, double y) {							FskCanvas2dTransform(ctx,	x,	0.,	0.,	y,	0.,	0.);	}
void FskCanvas2dTranslate(FskCanvas2dContext ctx, double x, double y) {							FskCanvas2dTransform(ctx,	1.,	0.,	0.,	1.,	x,	y);		}



/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *****									PATHS								*****
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/


/********************************************************************************
 * FskCanvas2dBeginPath
 ********************************************************************************/

FskErr
FskCanvas2dBeginPath(FskCanvas2dContext ctx)
{
	return ctx ? FskGrowablePathClear(ctx->path) : kFskErrInvalidParameter;
}


/********************************************************************************
 * FskCanvas2dClosePath
 ********************************************************************************/

FskErr
FskCanvas2dClosePath(FskCanvas2dContext ctx)
{
	return ctx ? FskGrowablePathAppendSegmentClose(ctx->path) : kFskErrInvalidParameter;
}


/********************************************************************************
 * FskCanvas2dMoveTo
 ********************************************************************************/

FskErr
FskCanvas2dMoveTo(FskCanvas2dContext ctx, double x, double y)
{
	return ctx ? FskGrowablePathAppendSegmentFloatMoveTo(x, y, ctx->path) : kFskErrInvalidParameter;
}


/********************************************************************************
 * FskCanvas2dLineTo
 ********************************************************************************/

FskErr
FskCanvas2dLineTo(FskCanvas2dContext ctx, double x, double y)
{
	return ctx ? FskGrowablePathAppendSegmentFloatLineTo(x, y, ctx->path) : kFskErrInvalidParameter;
}


/********************************************************************************
 * FskCanvas2dQuadraticCurveTo
 ********************************************************************************/

FskErr
FskCanvas2dQuadraticCurveTo(FskCanvas2dContext ctx, double cpx, double cpy, double x, double y)
{
	return ctx ? FskGrowablePathAppendSegmentFloatQuadraticBezierTo(cpx, cpy, x, y, ctx->path) : kFskErrInvalidParameter;
}


/********************************************************************************
 * FskCanvas2dCubicCurveTo
 ********************************************************************************/

FskErr
FskCanvas2dCubicCurveTo(FskCanvas2dContext ctx, double cp1x, double cp1y, double cp2x, double cp2y, double x, double y)
{
	return ctx ? FskGrowablePathAppendSegmentFloatCubicBezierTo(cp1x, cp1y, cp2x, cp2y, x, y, ctx->path) : kFskErrInvalidParameter;
}


/********************************************************************************
 * FskCanvas2dArc
 *
 * The arc(x, y, radius, startAngle, endAngle, anticlockwise) method draws an arc.
 * If the context has any subpaths, then the method must add a straight line
 * from the last point in the subpath to the start point of the arc.
 * In any case, it must draw the arc between the start point of the arc and the end point of the arc,
 * and add the start and end points of the arc to the subpath.
 *
 * The arc and its start and end points are defined as follows:
 * Consider a circle that has its origin at (x, y) and that has radius radius.
 * The points at startAngle and endAngle along this circle's circumference,
 * measured in radians clockwise from the positive x-axis, are the start and end points respectively.
 * If the anticlockwise argument is omitted or false and endAngle-startAngle is equal to or greater than 2π,
 * or, if the anticlockwise argument is true and startAngle-endAngle is equal to or greater than 2π,
 * then the arc is the whole circumference of this circle.
 * Otherwise, the arc is the path along the circumference of this circle from the start point to the end point,
 * going anti-clockwise if the anticlockwise argument is true, and clockwise otherwise.
 * Since the points are on the circle, as opposed to being simply angles from zero,
 * the arc can never cover an angle greater than 2π radians.
 * If the two points are the same, or if the radius is zero,
 * then the arc is defined as being of zero length in both directions.
 * Negative values for radius must cause the implementation to raise an INDEX_SIZE_ERR exception.
 ********************************************************************************/

FskErr
FskCanvas2dArc(FskCanvas2dContext ctx, double cx, double cy, double radius, double startAngle, double endAngle, Boolean counterClockwise)
{
	FskErr					err		= kFskErrNone;
	FskGrowablePath			path;
	FskFixedPoint2D			lastPt;
	FskDPoint2D				p, cp;
	double					dAngle, cd, sd, cpW, cpDist, t;
	int						nBez;

	if (!ctx)
		return kFskErrInvalidParameter;
	path = ctx->path;

	/* Determine the first point */
	p.x = cos(startAngle) * radius;
	p.y = sin(startAngle) * radius;

	/* Move or draw to the first point */
	if (kFskErrNone == FskGrowablePathGetLastPoint(path, &lastPt))
		BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatLineTo(p.x + cx, p.y + cy, path));
	else
		BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatMoveTo(p.x + cx, p.y + cy, path));

	/* Determine the last point */
	dAngle = endAngle - startAngle;
	if (dAngle > +D_2PI) {	endAngle = startAngle + (dAngle = +D_2PI); }
	if (dAngle < -D_2PI) {	endAngle = startAngle + (dAngle = -D_2PI); }
	if (counterClockwise) {
		if      (dAngle == +D_2PI)	dAngle = -D_2PI;
		else if (dAngle == -D_2PI)	dAngle = +D_2PI;
		else if (dAngle < 0)		dAngle += D_2PI;
		else if (dAngle > 0)		dAngle -= D_2PI;
	}

	nBez = (int)(fabs(dAngle) / D_2PI_3) + 1;		/* Number of rational Bezier segments */
	dAngle /= nBez;							/* Number of radians per segment */
	cd      = cos(dAngle);
	sd      = sin(dAngle);
	cpW    = cos(dAngle * .5);				/* Weight for control point */
	cpDist = radius / cpW;					/* Distance from center to control point */
	cp.x = cos(startAngle + dAngle * .5) * cpDist;
	cp.y = sin(startAngle + dAngle * .5) * cpDist;

	for (--nBez; nBez--; ) {
		t   = p.x * cd - p.y * sd;			/* Rotate start point to end */
		p.y = p.x * sd + p.y * cd;
		p.x = t;
		BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatRationalQuadraticBezierTo(cp.x + cx, cp.y + cy, cpW, p.x + cx, p.y + cy, path));
		t    = cp.x * cd - cp.y * sd;		/* Rotate control point */
		cp.y = cp.x * sd + cp.y * cd;
		cp.x = t;
	}
	p.x = cos(endAngle) * radius;	/* Avoid accumulation of eror at the end */
	p.y = sin(endAngle) * radius;
	BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatRationalQuadraticBezierTo(cp.x + cx, cp.y + cy, cpW, p.x + cx, p.y + cy, path));

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dArcTo
 *
 * The arcTo(x1, y1, x2, y2, radius) method must first ensure there is a subpath for (x1, y1).
 * Then, the behavior  depends on the arguments and the last point in the subpath, as described below.
 * Negative values for radius must cause the implementation to raise an INDEX_SIZE_ERR exception.
 * Let the point (x0, y0) be the last point in the subpath. If the point (x0, y0) is equal to the point (x1, y1),
 * or if the point (x1, y1) is equal to the point (x2, y2), or if the radius radius is zero,
 * then the method must add the point (x1, y1) to the subpath,
 * and connect that point to the previous point (x0, y0) by a straight line.
 * Otherwise, if the points (x0, y0), (x1, y1), and (x2, y2) all lie on a single straight line,
 * then the method must add the point (x1, y1) to the subpath, and connect that point
 * to the previous point (x0, y0) by a straight line.
 * Otherwise, let The Arc be the shortest arc given by circumference of the circle that has radius radius,
 * and that has one point tangent to the half-infinite line that crosses the point (x0, y0) and ends at the point (x1, y1),
 * and that has a different point tangent to the half-infinite line that ends at the point (x1, y1) and crosses the point (x2, y2).
 * The points at which this circle touches these two lines are called the start and end tangent points respectively.
 * The method must connect the point (x0, y0) to the start tangent point by a straight line,
 * adding the start tangent point to the subpath, and then must connect the start tangent point to the end tangent point by The Arc,
 * adding the end tangent point to the subpath.
 ********************************************************************************/

FskErr
FskCanvas2dArcTo(FskCanvas2dContext ctx, double x1, double y1, double x2, double y2, double radius)
{
	FskErr					err		= kFskErrNone;
	FskGrowablePath			path	= ctx->path;
	FskFixedPoint2D			lastPt;
	double					x0, y0;

	BAIL_IF_ERR(err = FskGrowablePathGetLastPoint(path, &lastPt));
	BAIL_IF_NEGATIVE(radius, err, kFskErrInvalidParameter);
	x0 = FskFixedToFloat(lastPt.x) - x1;			/* Coordinates relative to (x1, y1) */
	y0 = FskFixedToFloat(lastPt.y) - y1;
	x2 -= x1;
	y2 -= y1;
	if ((x0 == 0 && y0 == 0)	||
		(x2 == 0 && y2 == 0)	||
		(radius == 0)			||
		((x2 * y0 - x0 * y2) == 0)
	) {
		BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatLineTo(x1, y1, path));
	}
	else {
		double r, cosFull, sinFull, cotHalf, contact, w;
		r = hypot(x0, y0);	x0 /= r;	y0 /= r;	/* Normalize v0 */
		r = hypot(x2, y2);	x2 /= r;	y2 /= r;	/* Normalize v2 */
		cosFull = x0 * x2 + y0 * y2;				/*   dot(v0,v2) = cos angle between them */
		sinFull = x0 * y2 - y0 * x2;				/* cross(v0,v2) = sin angle between them */
		sinFull = fabs(sinFull);
		cotHalf = sinFull / (1. - cosFull);			/* cotangent of half the angle */
		contact = radius * cotHalf;					/* distance of contact from corner */
		x0 = x0 * contact + x1;						/* Absolute coordinates of the first  contact point X */
		y0 = y0 * contact + y1;						/* Absolute coordinates of the first  contact point Y */
		x2 = x2 * contact + x1;						/* Absolute coordinates of the second contact point X */
		y2 = y2 * contact + y1;						/* Absolute coordinates of the second contact point Y */
		w = (1 - cosFull) * .5;						/* sin^2 of the half angle */
		if      (w <= 0.)	w = 0;
		else if (w >= 1.)	w = 1.;
		else				w = sqrt(w);			/* sin of the half angle */
		BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatLineTo(x0, y0, path));
		if (cotHalf >= .5) {							/* Small enough angle to do the arc in one rational quadratic */
			BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatRationalQuadraticBezierTo(x1, y1, w, x2, y2, path));
		}
		else {										/* Subdivide once */
			double w01, x01, y01, w12, x12, y12, w012, x012, y012;
			x1 *= w;								/* Convert from rational to homogeneous */
			y1 *= w;
			w01  = (1.  +   w) * .5;				/* Homogeneous subdivision */
			x01  = (x0  +  x1) * .5;
			y01  = (y0  +  y1) * .5;
			w12  = (w   +  1.) * .5;
			x12  = (x1  +  x2) * .5;
			y12  = (y1  +  y2) * .5;
			w012 = (w01 + w12) * .5;
			x012 = (x01 + x12) * .5;
			y012 = (y01 + y12) * .5;
			x01  /= w01;							/* Convert from homogeneous to rational */
			y01  /= w01;
			x12  /= w12;
			y12  /= w12;
			x012 /= w012;
			y012 /= w012;
			w012  = sqrt(w012);
			w01  /= w012;							/* Canonical w with first and last 1 */
			w12  /= w012;
			BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatRationalQuadraticBezierTo(x01, y01, w01, x012, y012, path));
			BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatRationalQuadraticBezierTo(x12, y12, w12, x2,   y2,   path));
		}
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvasAppendPathString
 ********************************************************************************/

FskErr
FskCanvasAppendPathString(FskCanvas2dContext ctx, const char *pathStr)
{
	return FskGrowablePathAppendPathString(pathStr, ctx->path);
}


/********************************************************************************
 * FskCanvas2dRect
 ********************************************************************************/

FskErr
FskCanvas2dRect(FskCanvas2dContext ctx, double x, double y, double w, double h)
{
	FskErr					err		= kFskErrNone;
	FskGrowablePath			path	= ctx->path;

	BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatMoveTo(x,     y,     path));
	BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatLineTo(x + w, y,     path));
	BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatLineTo(x + w, y + h, path));
	BAIL_IF_ERR(err = FskGrowablePathAppendSegmentFloatLineTo(x,     y + h, path));
	BAIL_IF_ERR(err = FskGrowablePathAppendSegmentClose(ctx->path));
bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dClearRect
 *	Clear the specified rectangle to transparent black.
 ********************************************************************************/

FskErr
FskCanvas2dClearRect( FskCanvas2dContext ctx, double x, double y, double w, double h)
{
	FskErr					err		= kFskErrNone;
	FskCanvas2dContextState	*st		= FskCanvasGetStateFromContext(ctx);
	FskRectangleRecord		r;
	FskColorRGBARecord		color;

	FskRectangleSet(&r, FskRoundFloatToInt(x), FskRoundFloatToInt(y), FskRoundFloatToInt(w), FskRoundFloatToInt(h));	/* Quantize to integers */
	BAIL_IF_FALSE(FskRectangleIntersect(&st->clipRect, &r, &r), err, kFskErrNothingRendered);
	FskColorRGBASet(&color, 0, 0, 0, 0);
	if (st->clipBM == NULL) {
		err = FskRectangleFill(ctx->canvas->bm, &r, &color, 0, NULL);													/* Fill the given rectangle */
	}
	else {
		FskPointRecord dstPt;
		dstPt.x = 0;
		dstPt.y = 0;
		err = FskTransferAlphaBitmap(st->clipBM, NULL, ctx->canvas->bm, &dstPt, &r, &color, NULL);							/* Transfer the shape with color */
	}
bail:
	return err;
}


/********************************************************************************
 * FskComputeBoundingBoxOfTransformedRect
 ********************************************************************************/

static FskErr FskComputeBoundingBoxOfTransformedRect(double x, double y, double w, double h, FskFixedMatrix3x2 *M, FskFixedRectangle bbox) {
	FskFixedPoint2D	center, axes, wd, ht;

	/* Convert to center and axes in fixed point */
	axes.x   = (FskFixed)(w * (kFixedOne>>1) + .5);							/* half width,    rounded to nearest */
	axes.y   = (FskFixed)(h * (kFixedOne>>1) + .5);							/* half height,   rounded to nearest */
	center.x = (FskFixed)(x *  kFixedOne     + .5) + axes.x;				/* rect center x, rounded to nearest */
	center.y = (FskFixed)(y *  kFixedOne     + .5) + axes.y;				/* rect center y, rounded to nearest */

	FskTransformFixedRowPoints2D(&center, 1, M, &center);					/* Transform center */

	wd.x = FskFixMul(axes.x, M->M[0][0]);	if (wd.x < 0) wd.x = -wd.x;		/* Transform the axes */
	wd.y = FskFixMul(axes.y, M->M[1][0]);	if (wd.y < 0) wd.y = -wd.y;
	ht.x = FskFixMul(axes.x, M->M[0][1]);	if (ht.x < 0) ht.x = -ht.x;
	ht.y = FskFixMul(axes.y, M->M[1][1]);	if (ht.y < 0) ht.y = -ht.y;
	axes.x = wd.x + wd.y + 1;												/* Transformed half width,  rounded up to act as ceil */
	axes.y = ht.x + ht.y + 1;												/* Transformed half height, rounded up to act as ceil */

	bbox->x      = center.x - axes.x;										/* Convert from center/axis to upLeft/widthHeight */
	bbox->y      = center.y - axes.y;
	bbox->width  = axes.x << 1;
	bbox->height = axes.y << 1;

	return kFskErrNone;														/* TODO: detect when the bounds exceed the Fixed range */
}


/********************************************************************************
 * ComputeBoundingBoxOfFilledTransformedRect
 ********************************************************************************/

static FskErr ComputeBoundingBoxOfFilledTransformedRect(double x, double y, double w, double h, FskCanvas2dContextState *st, FskFixedRectangle bbox) {
	FskErr err = FskComputeBoundingBoxOfTransformedRect(x, y, w, h, &st->fixedTransform, bbox);
	EnlargeBoundingBox(bbox, 0);
	return err;
}


/********************************************************************************
 * ComputeBoundingBoxOfStrokedTransformedRect
 ********************************************************************************/

static FskErr ComputeBoundingBoxOfStrokedTransformedRect(double x, double y, double w, double h, FskCanvas2dContextState *st, FskFixedRectangle bbox) {
	FskErr err = FskComputeBoundingBoxOfTransformedRect(x, y, w, h, &st->fixedTransform, bbox);
	EnlargeBoundingBox(bbox, FskFixMul(FskFixMul(FskFixedMatrixNorm2x2(st->fixedTransform.M[0], 2), st->lineWidth), kFskLineJoinMiter90));	/* Rects are hard-wired to a 90 degree miter limit */
	return err;
}


/********************************************************************************
 * FskCanvas2dFillRect
 ********************************************************************************/

FskErr
FskCanvas2dFillRect(FskCanvas2dContext ctx, double x, double y, double w, double h)
{
	FskErr					err	= kFskErrNone;
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
	FskShapeRect			r;
	r.origin.x = FskRoundFloatToFixed(x);
	r.origin.y = FskRoundFloatToFixed(y);
	r.size.x   = FskRoundFloatToFixed(w);
	r.size.y   = FskRoundFloatToFixed(h);
	r.radius.x = 0;
	r.radius.y = 0;
	if (FskCanvasCanFillDirectly(st)) {
		err = FskFillShapeRect(&r, &st->fillStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);		/* Fill the rect with the style */
	}
	else {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfFilledTransformedRect(x, y, w, h, st, &bbox);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskFillShapeRect(&r, &st->fillStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dStrokeRect
 ********************************************************************************/

FskErr
FskCanvas2dStrokeRect(FskCanvas2dContext ctx, double x, double y, double w, double h)
{
	FskErr					err	= kFskErrNone;
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
	FskShapeRect			r;

	r.origin.x = FskRoundFloatToFixed(x);
	r.origin.y = FskRoundFloatToFixed(y);
	r.size.x   = FskRoundFloatToFixed(w);
	r.size.y   = FskRoundFloatToFixed(h);
	r.radius.x = 0;
	r.radius.y = 0;
	if (FskCanvasCanStrokeDirectly(st)) {
		err = FskFrameShapeRect(&r, st->lineWidth, &st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfStrokedTransformedRect(x, y, w, h, st, &bbox);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskFrameShapeRect(&r, st->lineWidth, &st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dFillCircle
 ********************************************************************************/

FskErr
FskCanvas2dFillCircle(FskCanvas2dContext ctx, double x, double y, double r)
{
	FskErr					err	= kFskErrNone;
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
	FskShapeCircle			circle;

	circle.center.x = FskRoundFloatToFixed(x);
	circle.center.y = FskRoundFloatToFixed(y);
	circle.radius   = FskRoundFloatToFixed(r);
	if (FskCanvasCanFillDirectly(st)) {
		err = FskFillShapeCircle(&circle, &st->fillStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfFilledTransformedRect(x-r, y-r, r*2., r*2., st, &bbox);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskFillShapeCircle(&circle, &st->fillStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dStrokeCircle
 ********************************************************************************/

FskErr
FskCanvas2dStrokeCircle(FskCanvas2dContext ctx, double x, double y, double r)
{
	FskErr					err	= kFskErrNone;
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
	FskShapeCircle			circle;

	circle.center.x = FskRoundFloatToFixed(x);
	circle.center.y = FskRoundFloatToFixed(y);
	circle.radius   = FskRoundFloatToFixed(r);
	if (FskCanvasCanStrokeDirectly(st)) {
		err = FskFrameShapeCircle(&circle, st->lineWidth, &st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfStrokedTransformedRect(x-r, y-r, r*2., r*2., st, &bbox);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskFrameShapeCircle(&circle, st->lineWidth, &st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dFillEllipse
 ********************************************************************************/

FskErr
FskCanvas2dFillEllipse(FskCanvas2dContext ctx, double x, double y, double rx, double ry)
{
	FskErr					err	= kFskErrNone;
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
	FskShapeEllipse			ellipse;

	ellipse.center.x = FskRoundFloatToFixed(x);
	ellipse.center.y = FskRoundFloatToFixed(y);
	ellipse.radius.x = FskRoundFloatToFixed(rx);
	ellipse.radius.y = FskRoundFloatToFixed(ry);

	if (FskCanvasCanFillDirectly(st)) {
		err = FskFillShapeEllipse(&ellipse, &st->fillStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfFilledTransformedRect(x-rx, y-ry, rx*2., ry*2., st, &bbox);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskFillShapeEllipse(&ellipse, &st->fillStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dStrokeEllipse
 ********************************************************************************/

FskErr
FskCanvas2dStrokeEllipse(FskCanvas2dContext ctx, double x, double y, double rx, double ry)
{
	FskErr					err	= kFskErrNone;
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
	FskShapeEllipse			ellipse;

	ellipse.center.x = FskRoundFloatToFixed(x);
	ellipse.center.y = FskRoundFloatToFixed(y);
	ellipse.radius.x = FskRoundFloatToFixed(rx);
	ellipse.radius.y = FskRoundFloatToFixed(ry);
	if (FskCanvasCanStrokeDirectly(st)) {
		err = FskFrameShapeEllipse(&ellipse, st->lineWidth, &st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfStrokedTransformedRect(x-rx, y-ry, rx*2., ry*2., st, &bbox);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskFrameShapeEllipse(&ellipse, st->lineWidth, &st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dFill
 ********************************************************************************/

FskErr
FskCanvas2dFill(FskCanvas2dContext ctx)
{
	FskErr					err	= kFskErrNone;
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dFill(ctx=%p)", ctx);
	#endif /* LOG_PARAMETERS */

	if (FskCanvasCanFillDirectly(st)) {
		err = FskGrowablePathFill(ctx->path, &st->fillStyle.csu.so, st->fillRule, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		FskPathComputeBoundingBox(FskGrowablePathGetConstPath(ctx->path), &st->fixedTransform, 0, &bbox);
		EnlargeBoundingBox(&bbox, 0);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskGrowablePathFill(ctx->path, &st->fillStyle.csu.so, st->fillRule, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dStroke
 ********************************************************************************/

FskErr
FskCanvas2dStroke(FskCanvas2dContext ctx)
{
	FskErr					err	= kFskErrNone;
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dStroke(ctx=%p)", ctx);
	#endif /* LOG_PARAMETERS */

	if (FskCanvasCanStrokeDirectly(st)) {
		err = FskGrowablePathFrame(ctx->path, st->lineWidth,  GetJointSharpnessFromState(st), st->lineCap,
							&st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		FskFixed pokeRadius = FskFixMul(FskFixedMatrixNorm2x2(st->fixedTransform.M[0], 2), st->lineWidth);	/* Scaled line width */
		if (st->miterLimit > 0x10000)																		/* If not always mitered or rounded ... */
			pokeRadius = FskFixMul(st->miterLimit, pokeRadius);												/* ... scale by the miter limit */
		FskPathComputeBoundingBox(FskGrowablePathGetConstPath(ctx->path), &st->fixedTransform, 0, &bbox);
		EnlargeBoundingBox(&bbox, pokeRadius);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskGrowablePathFrame(ctx->path, st->lineWidth,  GetJointSharpnessFromState(st), st->lineCap,
										&st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dFillText
 * maxWidth is optional and is ignored if <= 0,
 ********************************************************************************/

FskErr
FskCanvas2dFillText(FskCanvas2dContext ctx, const UInt16 *uniChars, double x, double y, double maxWidth)
{
	FskErr					err		= kFskErrNone;
	FskCanvas2dContextState	*st		= FskCanvasGetStateFromContext(ctx);
	FskGrowablePath			path	= NULL;

	BAIL_IF_ERR(err = FskCanvasGetTextPathThatFits(ctx, uniChars, x, y, maxWidth, &path));

	if (FskCanvasCanFillDirectly(st)) {
		err = FskGrowablePathFill(path, &st->fillStyle.csu.so, st->fillRule, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		FskPathComputeBoundingBox(FskGrowablePathGetConstPath(path), &st->fixedTransform, 0, &bbox);
		EnlargeBoundingBox(&bbox, 0);
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskGrowablePathFill(path, &st->fillStyle.csu.so, st->fillRule, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	FskGrowablePathDispose(path);
	return err;
}


/********************************************************************************
 * FskCanvas2dStrokeText
 * maxWidth is optional and is ignored if <= 0,
 ********************************************************************************/

FskErr
FskCanvas2dStrokeText(FskCanvas2dContext ctx, const UInt16 *uniChars, double x, double y, double maxWidth)
{
	FskErr					err		= kFskErrNone;
	FskCanvas2dContextState	*st		= FskCanvasGetStateFromContext(ctx);
	FskGrowablePath			path	= NULL;

	BAIL_IF_ERR(err = FskCanvasGetTextPathThatFits(ctx, uniChars, x, y, maxWidth, &path));

	if (FskCanvasCanStrokeDirectly(st)) {
		err = FskGrowablePathFrame(path, st->lineWidth,  GetJointSharpnessFromState(st), st->lineCap,
								   &st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->bm);
	}
	else {
		FskFixedRectangleRecord bbox;
		FskPathComputeBoundingBox(FskGrowablePathGetConstPath(ctx->path), &st->fixedTransform, 0, &bbox);	/* TODO: Look at only the first and last glyphs */
		EnlargeBoundingBox(&bbox, FskFixMul(FskFixedMatrixNorm2x2(st->fixedTransform.M[0], 2), st->lineWidth));
		ClearTempBufferTransparent(ctx);
		BAIL_IF_ERR(err = FskGrowablePathFrame(path, st->lineWidth,  GetJointSharpnessFromState(st), st->lineCap,
											   &st->strokeStyle.csu.so, &st->fixedTransform, st->quality, &st->clipRect, ctx->canvas->tmp));
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	FskGrowablePathDispose(path);
	return err;
}


/********************************************************************************
 * FskCanvas2dMeasureText
 ********************************************************************************/

double
FskCanvas2dMeasureText(FskCanvas2dContext ctx, const UInt16 *uniChars)
{
	FskCanvas2dContextState	*st		= FskCanvasGetStateFromContext(ctx);
	return FskFixedToFloat(FskUnicodeStringGetWidth(uniChars, FskUnicodeStrLen(uniChars), &st->font));
}


/********************************************************************************
 * FskCanvas2dClip
 ********************************************************************************/

FskErr
FskCanvas2dClip(FskCanvas2dContext ctx)
{
	FskErr						err	= kFskErrNone;
	FskCanvas2dContextState		*st = FskCanvasGetStateFromContext(ctx);
	FskConstBitmap				bm = ctx->canvas->bm;
	FskBitmapRecord				tmpClipBM;
	FskColorSourceConstant		color;
	FskFixedRectangleRecord		pathBounds;
	FskRectangleRecord			pathRect;

	BAIL_IF_FALSE(fskUniformChunkyPixelPacking == FskBitmapFormatPixelPacking(bm->pixelFormat), err, kFskErrUnsupportedPixelType);

	if (ctx->canvas->tmp == NULL) {
		BAIL_IF_ERR(err = FskBitmapNew(bm->bounds.width, bm->bounds.height, bm->pixelFormat, &ctx->canvas->tmp));
		FskBitmapSetHasAlpha(ctx->canvas->tmp, true);
		FskBitmapSetAlphaIsPremultiplied(ctx->canvas->tmp, true);
	}
	FskBitmapWriteBegin(ctx->canvas->tmp, NULL, NULL, NULL);

	if (st->clipBM == NULL) {	/* New clip region */
		BAIL_IF_ERR(err = FskBitmapNew(bm->bounds.width, bm->bounds.height, kFskBitmapFormat8G, &st->clipBM));
		ClearBufferToValue(st->clipBM, 255U);
	}

	/* Reinterpret the tmp buffer as alpha */
	tmpClipBM = *ctx->canvas->tmp;
	tmpClipBM.depth = 8;
	tmpClipBM.pixelFormat = kFskBitmapFormat8G;
	tmpClipBM.rowBytes = (tmpClipBM.rowBytes < 0) ? -tmpClipBM.bounds.width : tmpClipBM.bounds.width;

	/* Draw path */
	color.colorSource.type = kFskColorSourceTypeConstant;
	color.colorSource.dashCycles = 0;
	color.colorSource.dash = NULL;
	color.colorSource.dashPhase = 0;
	FskColorRGBASet(&color.color, 255U, 255U, 255U, 255U);
	ClearBufferToValue(&tmpClipBM, 0U);
	BAIL_IF_ERR(err = FskGrowablePathFill(ctx->path, &color.colorSource, kFskFillRuleWindingNumber, &st->fixedTransform, st->quality, &st->clipRect, &tmpClipBM));

	/* Intersect the previous matte with the new one */
	IntersectMattes(&tmpClipBM, NULL, st->clipBM, &st->clipRect);

	/* Compute the bounds of the new clip path, enlarge it for anti-aliasing, and intersect it with the clip rect */
	err = FskPathComputeBoundingBox(FskGrowablePathGetConstPath(ctx->path), &st->fixedTransform, 0, &pathBounds);
	EnlargeBoundingBox(&pathBounds, 0);
	FixedToIntBounds(&pathBounds, &pathRect);
	FskRectangleIntersect(&st->clipRect, &pathRect, &st->clipRect);

bail:
	if (ctx->canvas->tmp)	FskBitmapWriteEnd(ctx->canvas->tmp);
	return err;
}


/********************************************************************************
 * FskCanvas2dIsPointInPath
 ********************************************************************************/

Boolean
FskCanvas2dIsPointInPath(FskCanvas2dContext ctx, double x, double y)
{
	FskCanvas2dContextState *st = FskCanvasGetStateFromContext(ctx);
	FskPointRecord pt;
	long fillRule = kFskFillRuleWindingNumber;
	pt.x = FskRoundFloatToInt(x);
	pt.y = FskRoundFloatToInt(y);
	return kFskHitTrue == FskPickPathRadius(FskGrowablePathGetConstPath(ctx->path), st->lineWidth, GetJointSharpnessFromState(st),
											st->lineCap, fillRule, NULL, NULL, &st->fixedTransform, 1, &pt, NULL);
}


/********************************************************************************
 * FskCanvas2dDrawBitmap
 ********************************************************************************/

FskErr
FskCanvas2dDrawBitmap(FskCanvas2dContext ctx, FskConstBitmap src, double dx, double dy)
{
	FskCanvas2dContextState *st			= FskCanvasGetStateFromContext(ctx);
	Boolean					canDirect	= FskCanvasCanRenderDirectly(st);
	FskErr					err;
	FskBitmap				dst;
	FskRectangleRecord		dstRect;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dDrawBitmap(ctx=%p src=%p dx=%g dy=%g)", ctx, src, dx, dy);
		LogBitmap(src, "src");
	#endif /* LOG_PARAMETERS */

	if (canDirect) {						dst = ctx->canvas->bm;	}
	else { ClearTempBufferTransparent(ctx);	dst = ctx->canvas->tmp;  }

	dstRect.width  = src->bounds.width;
	dstRect.height = src->bounds.height;
	if (FskCanvasStateMatrixIsIdentity(st)) {
		Boolean	hasAlpha;
		UInt32	mode;
		mode = ((FskBitmapGetHasAlpha(src, &hasAlpha), hasAlpha) ? kFskGraphicsModeAlpha : kFskGraphicsModeCopy);
		if (st->quality)
			mode |= kFskGraphicsModeBilinear;
		dstRect.x = FskRoundFloatToInt(dx);				// TODO: should check for integral coordinates
		dstRect.y = FskRoundFloatToInt(dy);				// TODO: Use FskScaleOffsetBitmap instead?
		err = FskBitmapDraw(src, NULL, dst, &dstRect, &st->clipRect, NULL, mode, NULL);
	} else {
		err = DrawTransformedBitmap(st, src, 0, 0, dstRect.width, dstRect.height, dst, dx, dy, dstRect.width, dstRect.height);
	}
	BAIL_IF_ERR(err);
	if (!canDirect) {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfFilledTransformedRect(dx, dy, dstRect.width, dstRect.height, st, &bbox);
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dDrawScaledBitmap
 ********************************************************************************/

FskErr
FskCanvas2dDrawScaledBitmap(FskCanvas2dContext ctx, FskConstBitmap src, double dx, double dy, double dw, double dh)
{
	FskCanvas2dContextState *st			= FskCanvasGetStateFromContext(ctx);
	Boolean					canDirect	= FskCanvasCanRenderDirectly(st);
	FskErr					err;
	FskBitmap				dst;
	FskRectangleRecord		dstRect;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dDrawScaledBitmap(ctx=%p src=%p dx=%g dy=%g dw=%g dh=%g)", ctx, src, dx, dy, dw, dh);
		LogBitmap(src, "src");
	#endif /* LOG_PARAMETERS */

	if (canDirect) {						dst = ctx->canvas->bm;	}
	else { ClearTempBufferTransparent(ctx);	dst = ctx->canvas->tmp;	}

	dstRect.width  = FskRoundFloatToInt(dw);
	dstRect.height = FskRoundFloatToInt(dh);
	if (FskCanvasStateMatrixIsIdentity(st)) {
		Boolean	hasAlpha;
		UInt32	mode;
		mode = ((FskBitmapGetHasAlpha(src, &hasAlpha), hasAlpha) ? kFskGraphicsModeAlpha : kFskGraphicsModeCopy);
		if (st->quality)
			mode |= kFskGraphicsModeBilinear;
		dstRect.x = FskRoundFloatToInt(dx);				// TODO: should check for integral coordinates
		dstRect.y = FskRoundFloatToInt(dy);				// TODO: Use FskScaleOffsetBitmap instead?
		err = FskBitmapDraw(src, NULL, dst, &dstRect, &st->clipRect, NULL, mode, NULL);
	} else {
		err = DrawTransformedBitmap(st, src, 0, 0, src->bounds.width, src->bounds.height, dst, dx, dy, dw, dh);
	}
	BAIL_IF_ERR(err);

	if (!canDirect) {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfFilledTransformedRect(dx, dy, dw, dh, st, &bbox);
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dDrawSubScaledBitmap
 ********************************************************************************/

FskErr
FskCanvas2dDrawSubScaledBitmap(FskCanvas2dContext ctx, FskConstBitmap src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh)
{
	Boolean					hasAlpha;
	FskErr					err			= FskBitmapGetHasAlpha(src, &hasAlpha);
	FskCanvas2dContextState *st			= FskCanvasGetStateFromContext(ctx);
	Boolean					canDirect	= FskCanvasCanRenderDirectly(st) && !hasAlpha;
	FskBitmap				dst;

	#if defined(LOG_PARAMETERS)
		LOGD("FskCanvas2dDrawSubScaledBitmap(ctx=%p src=%p dx=%g dy=%g dw=%g dh=%g)", ctx, src, sx, sy, sw, sh, dx, dy, dw, dh);
		LogBitmap(src, "src");
	#endif /* LOG_PARAMETERS */

	BAIL_IF_ERR(err);

	if (canDirect) {						dst = ctx->canvas->bm;	}
	else { ClearTempBufferTransparent(ctx);	dst = ctx->canvas->tmp;	}

	if (FskCanvasStateMatrixIsIdentity(st)) {
		FskRectangleRecord	srcRect, dstRect;
		Boolean	hasAlpha;
		UInt32	mode;
		mode = ((FskBitmapGetHasAlpha(src, &hasAlpha), hasAlpha) ? kFskGraphicsModeAlpha : kFskGraphicsModeCopy);
		if (st->quality)
			mode |= kFskGraphicsModeBilinear;
		srcRect.x      = FskRoundFloatToInt(sx);				// TODO: should check for integral coordinates
		srcRect.y      = FskRoundFloatToInt(sy);				// TODO: Use FskScaleOffsetBitmap instead?
		srcRect.width  = FskRoundFloatToInt(sw);
		srcRect.height = FskRoundFloatToInt(sh);
		dstRect.x      = FskRoundFloatToInt(dx);
		dstRect.y      = FskRoundFloatToInt(dy);
		dstRect.width  = FskRoundFloatToInt(dw);
		dstRect.height = FskRoundFloatToInt(dh);
		err = FskBitmapDraw(src, &srcRect, dst, &dstRect, &st->clipRect, NULL, mode, NULL);
	} else {
		err = DrawTransformedBitmap(st, src, sx, sy, sw, sh, dst, dx, dy, dw, dh);
	}
	BAIL_IF_ERR(err);

	if (!canDirect) {
		FskFixedRectangleRecord bbox;
		ComputeBoundingBoxOfFilledTransformedRect(dx, dy, dw, dh, st, &bbox);
		err = ShadeMatteRect(ctx, &bbox);
	}

bail:
	return err;
}


/********************************************************************************
 * FskCanvas2dDrawCanvas
 ********************************************************************************/

FskErr
FskCanvas2dDrawCanvas2d(FskCanvas2dContext ctx, FskConstCanvas src, double dx, double dy)
{
	return FskCanvas2dDrawBitmap(ctx, src->bm, dx, dy);
}


/********************************************************************************
 * FskCanvas2dDrawScaledCanvas
 ********************************************************************************/

FskErr
FskCanvas2dDrawScaledCanvas2d(FskCanvas2dContext ctx, FskConstCanvas src, double dx, double dy, double dw, double dh)
{
	return FskCanvas2dDrawScaledBitmap(ctx, src->bm, dx, dy, dw, dh);
}


/********************************************************************************
 * FskCanvas2dDrawSubScaledCanvas
 ********************************************************************************/

FskErr
FskCanvas2dDrawSubScaledCanvas2d(FskCanvas2dContext ctx, FskConstCanvas src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh)
{
	return FskCanvas2dDrawSubScaledBitmap(ctx, src->bm, sx, sy, sw, sh, dx, dy, dw, dh);
}


/********************************************************************************
 * FskCanvas2dDrawVideo
 ********************************************************************************/

FskErr
FskCanvas2dDrawVideo(FskCanvas2dContext ctx, FskVideo src, double dx, double dy)
{
	return kFskErrUnimplemented;
}


/********************************************************************************
 * FskCanvas2dDrawScaledVideo
 ********************************************************************************/

FskErr
FskCanvas2dDrawScaledVideo(FskCanvas2dContext ctx, FskVideo src, double dx, double dy, double dw, double dh)
{
	return kFskErrUnimplemented;
}


/********************************************************************************
 * FskCanvas2dDrawSubScaledVideo
 ********************************************************************************/

FskErr
FskCanvas2dDrawSubScaledVideo(FskCanvas2dContext ctx, FskVideo src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh)
{
	return kFskErrUnimplemented;
}


/********************************************************************************
 * FskCanvas2dCreateImageData
 ********************************************************************************/

FskCanvas2dImageData
FskCanvas2dCreateImageData(FskConstCanvas2dContext ctx, double sw, double sh)
{
	FskErr					err			= kFskErrNone;
	FskCanvas2dImageData	id			= NULL;
	long					w			= (long)sw;
	long					h			= (long)sh;

	size_t					dataLength	= w * h * sizeof(UInt32);

	BAIL_IF_ERR(err = FskMemPtrNew(sizeof(*id) + dataLength, &id));
	id->width       = w;
	id->height      = h;
	id->data.length = dataLength;
	id->data.bytes  = (UInt8*)(id + 1);

bail:
	return id;
}


/********************************************************************************
 * FskCanvas2dDisposeImageData
 ********************************************************************************/

void
FskCanvas2dDisposeImageData(FskCanvas2dImageData id)
{
	FskMemPtrDispose(id);
}


/********************************************************************************
 * FskCanvas2dCloneImageData
 ********************************************************************************/

FskCanvas2dImageData
FskCanvas2dCloneImageData(FskConstCanvas2dContext ctx, FskCanvas2dImageData imageData)
{
	FskErr					err			= kFskErrNone;
	FskCanvas2dImageData	id			= NULL;

	BAIL_IF_ERR(err = FskMemPtrNew(sizeof(*id) + imageData->data.length, &id));
	*id = *imageData;
	id->data.bytes = (UInt8*)(id + 1);
	FskMemCopy(id->data.bytes, imageData->data.bytes, id->data.length);

bail:
	return id;
}


/********************************************************************************
 * FskCanvas2dGetImageData
 ********************************************************************************/

FskCanvas2dImageData
FskCanvas2dGetImageData(FskConstCanvas2dContext ctx, double sx, double sy, double sw, double sh)
{
	FskCanvas2dImageData	id		= NULL;
	SInt32					x		= (SInt32)sx;
	SInt32					y		= (SInt32)sy;
	SInt32					w		= (SInt32)sw;
	SInt32					h		= (SInt32)sh;
	FskBitmapRecord			bmr;
	FskRectangleRecord		srcRect, dstRect;

	if ((id = FskCanvas2dCreateImageData(ctx, sw, sh)) == NULL)
		goto bail;

	FskMemSet(&bmr, 0, sizeof(bmr));
	bmr.bounds.width  = w;
	bmr.bounds.height = h;
	bmr.depth         = 32;
	bmr.pixelFormat   = kFskBitmapFormat32RGBA;
	bmr.rowBytes      = w * sizeof(UInt32);
	bmr.bits          = id->data.bytes;

	srcRect.x         = x;
	srcRect.y         = y;
	srcRect.width     = w;
	srcRect.height    = h;
	dstRect.x         = 0;
	dstRect.y         = 0;
	dstRect.width     = w;
	dstRect.height    = h;

	#if defined(DST_32RGBA) && DST_32RGBA
		if (FskBitmapDraw(ctx->canvas->bm, &srcRect, &bmr, &dstRect, NULL, NULL, kFskGraphicsModeCopy, NULL) != kFskErrNone)
	#endif /* DST_32RGBA */
	{
		bmr.pixelFormat = kFskBitmapFormatDefaultRGBA;
		if ((FskBitmapDraw(ctx->canvas->bm, &srcRect, &bmr, &dstRect, NULL, NULL, kFskGraphicsModeCopy, NULL) != kFskErrNone)	||
			(FskCanvasConvertImageDataFormat(id, kFskBitmapFormatDefaultRGBA, kFskBitmapFormat32RGBA) != kFskErrNone)
		) {
			FskCanvas2dDisposeImageData(id);
			id = NULL;
		}
	}

bail:
	return id;
}


/********************************************************************************
 * FskCanvas2dPutImageData
 ********************************************************************************/

FskErr
FskCanvas2dPutImageData(FskCanvas2dContext ctx, FskConstCanvas2dImageData src, double dx, double dy, double sx, double sy, double sw, double sh)
{
	FskErr				err		= kFskErrNone;
	SInt32				srcX	= (SInt32)sx;
	SInt32				srcY	= (SInt32)sy;
	SInt32				srcW	= (SInt32)sw;
	SInt32				srcH	= (SInt32)sh;
	FskBitmapRecord		bmr;
	FskRectangleRecord	srcRect, dstRect;
	SInt32				t;

	if (srcW < 0) {
		srcX += srcW;
		srcW = -srcW;
	}
	if (srcX < 0) {
		srcW += srcX;
		srcX = 0;
	}
	if (srcW > (t = src->width - srcX))
		srcW = t;

	if (srcH < 0) {
		srcY += srcH;
		srcH = -srcH;
	}
	if (srcY < 0) {
		srcH += srcY;
		srcY = 0;
	}
	if (srcH > (t = src->height - srcY))
		srcH = t;

	if (srcW <= 0 || srcH <= 0)
		goto bail;

	srcRect.x         = srcX;
	srcRect.y         = srcY;
	srcRect.width     = srcW;
	srcRect.height    = srcH;
	dstRect.x         = (SInt32)dx;
	dstRect.y         = (SInt32)dy;
	dstRect.width     = srcW;
	dstRect.height    = srcH;

	FskMemSet(&bmr, 0, sizeof(bmr));
	bmr.bounds.width  = src->width;
	bmr.bounds.height = src->height;
	bmr.depth         = 32;
	bmr.pixelFormat   = kFskBitmapFormat32RGBA;
	bmr.rowBytes      = src->width * sizeof(UInt32);
	bmr.bits          = src->data.bytes;

	#if defined(SRC_32RGBA) && SRC_32RGBA
		if ((err = FskBitmapDraw(&bmr, &srcRect, ctx->canvas->bm, &dstRect, NULL, NULL, kFskGraphicsModeCopy, NULL)) != kFskErrNone)
	#endif /* SRC_32RGBA */
	{
		BAIL_IF_ERR(err = FskCanvasConvertImageDataFormat((FskCanvas2dImageData)src, kFskBitmapFormat32RGBA, kFskBitmapFormatDefaultRGBA));
		bmr.pixelFormat = kFskBitmapFormatDefaultRGBA;
		err = FskBitmapDraw(&bmr, &srcRect, ctx->canvas->bm, &dstRect, NULL, NULL, kFskGraphicsModeCopy, NULL);
		FskCanvasConvertImageDataFormat((FskCanvas2dImageData)src, kFskBitmapFormatDefaultRGBA, kFskBitmapFormat32RGBA);
	}

bail:
	return err;
}

FskErr
FskCanvas2dSetOpenGLSourceAccelerated(FskCanvas cnv, Boolean accelerated)
{
    FskErr err = kFskErrNone;

    cnv->accelerate = accelerated;
    if (cnv->bm)
        err = FskBitmapSetOpenGLSourceAccelerated(cnv->bm, true);

    return err;
}
