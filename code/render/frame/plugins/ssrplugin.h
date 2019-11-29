#pragma once
//------------------------------------------------------------------------------
/**
	Implements screen space reflections as a script plugin
	
	(C) 2019 Individual contributors, see AUTHORS file
*/
//------------------------------------------------------------------------------
#include "frameplugin.h"
#include "coregraphics/rendertexture.h"
#include "coregraphics/shader.h"
#include "coregraphics/resourcetable.h"
// #include "renderutil/drawfullscreenquad.h"
#include "ssr_cs.h"
namespace Frame
{
class SSRPlugin : public FramePlugin
{
public:
	/// constructor
    SSRPlugin();
	/// destructor
	virtual ~SSRPlugin();

	/// setup
	void Setup();
	/// discard
	void Discard();

private:
    CoreGraphics::ShaderId shader;
	Util::FixedArray<CoreGraphics::ResourceTableId> ssrTables;
	IndexT zThicknessSlot;
	IndexT pixelStrideSlot;
	IndexT maxStepsSlot;
    IndexT maxDistanceSlot;
    IndexT ssrBufferSlot;
    IndexT constantsSlot;
    SsrCs::SSRBlock ssrBlock;


	CoreGraphics::ShaderProgramId program;

	CoreGraphics::ConstantBufferId constants;
};

} // namespace Algorithms
