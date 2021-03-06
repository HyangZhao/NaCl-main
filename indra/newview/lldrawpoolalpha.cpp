/** 
 * @file lldrawpoolalpha.cpp
 * @brief LLDrawPoolAlpha class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "lldrawpoolalpha.h"

#include "llglheaders.h"
#include "llviewercontrol.h"
#include "llcriticaldamp.h"
#include "llfasttimer.h"
#include "llrender.h"

#include "llcubemap.h"
#include "llsky.h"
#include "lldrawable.h"
#include "llface.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"	// For debugging
#include "llviewerobjectlist.h" // For debugging
#include "llviewerwindow.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewerregion.h"
#include "lldrawpoolwater.h"
#include "llspatialpartition.h"

BOOL LLDrawPoolAlpha::sShowDebugAlpha = FALSE;

static BOOL deferred_render = FALSE;

LLDrawPoolAlpha::LLDrawPoolAlpha(U32 type) :
		LLRenderPass(type), current_shader(NULL), target_shader(NULL),
		simple_shader(NULL), fullbright_shader(NULL)
{

}

LLDrawPoolAlpha::~LLDrawPoolAlpha()
{
}


void LLDrawPoolAlpha::prerender()
{
	mVertexShaderLevel = LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

S32 LLDrawPoolAlpha::getNumDeferredPasses()
{
	return 1;
}

void LLDrawPoolAlpha::beginDeferredPass(S32 pass)
{
	
}

void LLDrawPoolAlpha::endDeferredPass(S32 pass)
{
	
}

void LLDrawPoolAlpha::renderDeferred(S32 pass)
{
	gGL.setAlphaRejectSettings(LLRender::CF_GREATER, 0.f);
	{
		LLFastTimer t(FTM_RENDER_GRASS);
		gDeferredTreeProgram.bind();
		LLGLEnable test(GL_ALPHA_TEST);
		//render alpha masked objects
		LLRenderPass::renderTexture(LLRenderPass::PASS_ALPHA_MASK, getVertexDataMask());
		gDeferredTreeProgram.unbind();
	}			
	gGL.setAlphaRejectSettings(LLRender::CF_DEFAULT);
}


S32 LLDrawPoolAlpha::getNumPostDeferredPasses() 
{ 
	return 1; 
}

void LLDrawPoolAlpha::beginPostDeferredPass(S32 pass) 
{ 
	LLFastTimer t(FTM_RENDER_ALPHA);

	simple_shader = &gDeferredAlphaProgram;
	fullbright_shader = &gDeferredFullbrightProgram;
	
	deferred_render = TRUE;
	if (mVertexShaderLevel > 0)
	{
		// Start out with no shaders.
		current_shader = target_shader = NULL;
	}
	gPipeline.enableLightsDynamic();
}

void LLDrawPoolAlpha::endPostDeferredPass(S32 pass) 
{ 
	deferred_render = FALSE;
	endRenderPass(pass);
}

void LLDrawPoolAlpha::renderPostDeferred(S32 pass) 
{ 
	render(pass); 
}

void LLDrawPoolAlpha::beginRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA);
	
	if (LLPipeline::sUnderWaterRender)
	{
		simple_shader = &gObjectSimpleWaterProgram;
		fullbright_shader = &gObjectFullbrightWaterProgram;
	}
	else
	{
		simple_shader = &gObjectSimpleProgram;
		fullbright_shader = &gObjectFullbrightProgram;
	}

	if (mVertexShaderLevel > 0)
	{
		// Start out with no shaders.
		current_shader = target_shader = NULL;
		LLGLSLShader::bindNoShader();
	}
	gPipeline.enableLightsDynamic();
}

void LLDrawPoolAlpha::endRenderPass( S32 pass )
{
	LLFastTimer t(FTM_RENDER_ALPHA);
	LLRenderPass::endRenderPass(pass);

	if(gPipeline.canUseWindLightShaders()) 
	{
		LLGLSLShader::bindNoShader();
	}
}

void LLDrawPoolAlpha::render(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA);

	LLGLSPipelineAlpha gls_pipeline_alpha;

	if (LLPipeline::sFastAlpha && !deferred_render)
	{
		LLGLDisable blend_disable(GL_BLEND);
		gGL.setAlphaRejectSettings(LLRender::CF_GREATER, 0.33f);
		if (mVertexShaderLevel > 0)
		{
			if (!LLPipeline::sRenderDeferred)
			{
				simple_shader->bind();
				pushBatches(LLRenderPass::PASS_ALPHA_MASK, getVertexDataMask());
			}
			fullbright_shader->bind();
			pushBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, getVertexDataMask());
			LLGLSLShader::bindNoShader();
		}
		else
		{
			gPipeline.enableLightsFullbright(LLColor4(1,1,1,1));
			pushBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, getVertexDataMask());
			gPipeline.enableLightsDynamic();
			pushBatches(LLRenderPass::PASS_ALPHA_MASK, getVertexDataMask());
		}
		gGL.setAlphaRejectSettings(LLRender::CF_DEFAULT);
	}

	LLGLDepthTest depth(GL_TRUE, LLDrawPoolWater::sSkipScreenCopy ? GL_TRUE : GL_FALSE);
	renderAlpha(getVertexDataMask());

	if (deferred_render && current_shader != NULL)
	{
		gPipeline.unbindDeferredShader(*current_shader);
	}

	if (sShowDebugAlpha)
	{
		if(gPipeline.canUseWindLightShaders()) 
		{
			LLGLSLShader::bindNoShader();
		}
		gPipeline.enableLightsFullbright(LLColor4(1,1,1,1));
		glColor4f(1,0,0,1);
		LLViewerFetchedTexture::sSmokeImagep->addTextureStats(1024.f*1024.f);
		gGL.getTexUnit(0)->bind(LLViewerFetchedTexture::sSmokeImagep, TRUE) ;
		renderAlphaHighlight(LLVertexBuffer::MAP_VERTEX |
							LLVertexBuffer::MAP_TEXCOORD0);
	}
}

void LLDrawPoolAlpha::renderAlphaHighlight(U32 mask)
{
	for (LLCullResult::sg_list_t::iterator i = gPipeline.beginAlphaGroups(); i != gPipeline.endAlphaGroups(); ++i)
	{
		LLSpatialGroup* group = *i;
		if (group->mSpatialPartition->mRenderByGroup &&
			!group->isDead())
		{
			LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[LLRenderPass::PASS_ALPHA];	

			for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)	
			{
				LLDrawInfo& params = **k;
				
				if (params.mParticle)
				{
					continue;
				}

				LLRenderPass::applyModelMatrix(params);
				if (params.mGroup)
				{
					params.mGroup->rebuildMesh();
				}
				params.mVertexBuffer->setBuffer(mask);
				params.mVertexBuffer->drawRange(params.mDrawMode, params.mStart, params.mEnd, params.mCount, params.mOffset);
				gPipeline.addTrianglesDrawn(params.mCount, params.mDrawMode);
			}
		}
	}
}

void LLDrawPoolAlpha::renderAlpha(U32 mask)
{
	BOOL initialized_lighting = FALSE;
	BOOL light_enabled = TRUE;
	S32 diffuse_channel = 0;

	//BOOL is_particle = FALSE;
	BOOL use_shaders = (LLPipeline::sUnderWaterRender && gPipeline.canUseVertexShaders())
		|| gPipeline.canUseWindLightShadersOnObjects();
	
	// check to see if it's a particle and if it's "close"
	{
		if (LLPipeline::sImpostorRender)
		{
			gGL.setAlphaRejectSettings(LLRender::CF_GREATER, 0.5f);
		}
		else
		{
			gGL.setAlphaRejectSettings(LLRender::CF_DEFAULT);
		}
	}

	for (LLCullResult::sg_list_t::iterator i = gPipeline.beginAlphaGroups(); i != gPipeline.endAlphaGroups(); ++i)
	{
		LLSpatialGroup* group = *i;
		if (group->mSpatialPartition->mRenderByGroup &&
			!group->isDead())
		{
			LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[LLRenderPass::PASS_ALPHA];

			for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)	
			{
				LLDrawInfo& params = **k;

				LLRenderPass::applyModelMatrix(params);

				if (params.mFullbright)
				{
					// Turn off lighting if it hasn't already been so.
					if (light_enabled || !initialized_lighting)
					{
						initialized_lighting = TRUE;
						if (use_shaders) 
						{
							target_shader = fullbright_shader;
						}
						else
						{
							gPipeline.enableLightsFullbright(LLColor4(1,1,1,1));
						}
						light_enabled = FALSE;
					}
				}
				// Turn on lighting if it isn't already.
				else if (!light_enabled || !initialized_lighting)
				{
					initialized_lighting = TRUE;
					if (use_shaders) 
					{
						target_shader = simple_shader;
					}
					else
					{
						gPipeline.enableLightsDynamic();
					}
					light_enabled = TRUE;
				}

				// If we need shaders, and we're not ALREADY using the proper shader, then bind it
				// (this way we won't rebind shaders unnecessarily).
				if(use_shaders && (current_shader != target_shader))
				{
					llassert(target_shader != NULL);
					if (deferred_render && current_shader != NULL)
					{
						gPipeline.unbindDeferredShader(*current_shader);
						diffuse_channel = 0;
					}
					current_shader = target_shader;
					if (deferred_render)
					{
						gPipeline.bindDeferredShader(*current_shader);
						diffuse_channel = current_shader->enableTexture(LLViewerShaderMgr::DIFFUSE_MAP);
					}
					else
					{
						current_shader->bind();
					}
				}
				else if (!use_shaders && current_shader != NULL)
				{
					if (deferred_render)
					{
						gPipeline.unbindDeferredShader(*current_shader);
						diffuse_channel = 0;
					}
					LLGLSLShader::bindNoShader();
					current_shader = NULL;
				}

				if (params.mGroup)
				{
					params.mGroup->rebuildMesh();
				}

				
				if (params.mTexture.notNull())
				{
					gGL.getTexUnit(diffuse_channel)->bind(params.mTexture.get());
					if(params.mTexture.notNull())
					{
						params.mTexture->addTextureStats(params.mVSize);
					}
					if (params.mTextureMatrix)
					{
						gGL.getTexUnit(0)->activate();
						glMatrixMode(GL_TEXTURE);
						glLoadMatrixf((GLfloat*) params.mTextureMatrix->mMatrix);
						gPipeline.mTextureMatrixOps++;
					}
				}

				params.mVertexBuffer->setBuffer(mask);
				params.mVertexBuffer->drawRange(params.mDrawMode, params.mStart, params.mEnd, params.mCount, params.mOffset);
				gPipeline.addTrianglesDrawn(params.mCount, params.mDrawMode);

				if (params.mTextureMatrix && params.mTexture.notNull())
				{
					gGL.getTexUnit(0)->activate();
					glLoadIdentity();
					glMatrixMode(GL_MODELVIEW);
				}
			}
		}
	}

	if (deferred_render && current_shader != NULL)
	{
		gPipeline.unbindDeferredShader(*current_shader);
		LLVertexBuffer::unbind();	
		LLGLState::checkStates();
		LLGLState::checkTextureChannels();
		LLGLState::checkClientArrays();
	}
	
	if (!light_enabled)
	{
		gPipeline.enableLightsDynamic();
	}
}
