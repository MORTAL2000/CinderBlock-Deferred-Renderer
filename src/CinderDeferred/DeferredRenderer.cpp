//
//  DeferredRenderer.cpp
//  CinderDeferredRendering
//
//  Created by Anthony Scavarelli on 2012-12-31.
//
//

#include "DeferredRenderer.h"

vector<Light_Point*>* DeferredRenderer::getPointLightsRef(){
    return &mPointLights;
};

const int DeferredRenderer::getNumPointLights(){
    return mPointLights.size();
};

vector<Light_Spot*>* DeferredRenderer::getSpotLightsRef(){
    return &mSpotLights;
};

const int DeferredRenderer::getNumSpotLights(){
    return mSpotLights.size();
};

void DeferredRenderer::setup(   Camera    *cam,
                                Vec2i     FBORes,
                                int       shadowMapRes,
                                int       deferFlags,
                                const     boost::function<void()> renderOverlayFunc,
                                const     boost::function<void()> renderParticlesFunc )
{
    //create cube VBO reference for lights
    mCubeVBOMesh = DeferredModel::getCubeVboMesh( Vec3f(0.0f, 0.0f, 0.0f), Vec3f(1.0f, 1.0f, 1.0f) );
    mConeVBOMesh = DeferredModel::getConeVboMesh( Vec3f(0.0f, 0.0f, 0.0f), 1.0f, 0.5f, 30);
    mSphereVBOMesh = DeferredModel::getSphereVboMesh( Vec3f(0.0f, 0.0f, 0.0f), 0.5f, Vec2i(30,30));
    mFSQuadVBOMesh = DeferredModel::getFullScreenVboMesh();
    
    mOrthoCam = CameraOrtho(-1, 1, -1, 1, 0, 1);
    
    fRenderOverlayFunc = renderOverlayFunc;
    fRenderParticlesFunc = renderParticlesFunc;
    mCam = cam;
    mFBOResolution = FBORes;
    mShadowMapRes = shadowMapRes;
    
    mDeferFlags = deferFlags;
    
    glClearDepth(1.0f);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glDisable(GL_LIGHTING);
    glClearColor(0, 0, 0, 0);
    glColor4d(1, 1, 1, 1);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable( GL_TEXTURE_2D );
    
    //axial matrices required for six-sides of calculations for cube shadows
    CameraPersp cubeCam;
    cubeCam.lookAt( Vec3f(0.0, 0.0, 0.0),  Vec3f(1.0, 0.0, 0.0),  Vec3f(0.0,-1.0, 0.0));
    mLightFaceViewMatrices[ DeferredMaps::CubeShadowMap::X_FACE_POS ] = cubeCam.getModelViewMatrix();
    cubeCam.lookAt( Vec3f(0.0, 0.0, 0.0), Vec3f(-1.0, 0.0, 0.0),  Vec3f(0.0,-1.0, 0.0));
    mLightFaceViewMatrices[ DeferredMaps::CubeShadowMap::X_FACE_NEG ] = cubeCam.getModelViewMatrix();
    cubeCam.lookAt( Vec3f(0.0, 0.0, 0.0),  Vec3f(0.0, 1.0, 0.0),  Vec3f(0.0, 0.0, 1.0));
    mLightFaceViewMatrices[ DeferredMaps::CubeShadowMap::Y_FACE_POS ] = cubeCam.getModelViewMatrix();
    cubeCam.lookAt( Vec3f(0.0, 0.0, 0.0),  Vec3f(0.0,-1.0, 0.0),  Vec3f(0.0, 0.0,-1.0) );
    mLightFaceViewMatrices[ DeferredMaps::CubeShadowMap::Y_FACE_NEG ] = cubeCam.getModelViewMatrix();
    cubeCam.lookAt( Vec3f(0.0, 0.0, 0.0),  Vec3f(0.0, 0.0, 1.0),  Vec3f(0.0,-1.0, 0.0) );
    mLightFaceViewMatrices[ DeferredMaps::CubeShadowMap::Z_FACE_POS ] = cubeCam.getModelViewMatrix();
    cubeCam.lookAt( Vec3f(0.0, 0.0, 0.0),  Vec3f(0.0, 0.0,-1.0),  Vec3f(0.0,-1.0, 0.0) );
    mLightFaceViewMatrices[ DeferredMaps::CubeShadowMap::Z_FACE_NEG ] = cubeCam.getModelViewMatrix();
    
    initTextures();
    initFBOs();
    initShaders();
}

Light_Point* DeferredRenderer::addPointLight(const Vec3f position, const Color color, const float intensity, const bool castsShadows, const bool visible)
{
    Light_Point *newLightP = new Light_Point( &mCubeVBOMesh, position, color, intensity, mShadowMapRes, (castsShadows && (mDeferFlags & SHADOWS_ENABLED_FLAG)), visible );
    mPointLights.push_back( newLightP );
    return newLightP;
}

Light_Spot* DeferredRenderer::addSpotLight(const Vec3f position, const Vec3f target, const Color color, const float intensity, const float lightAngle, const bool castsShadows, const bool visible)
{
    Light_Spot *newLightP = new Light_Spot( &mFSQuadVBOMesh, position, target, color, intensity, lightAngle, mShadowMapRes, (castsShadows && (mDeferFlags & SHADOWS_ENABLED_FLAG)), visible );
    mSpotLights.push_back( newLightP );
    return newLightP;
}

DeferredModel* DeferredRenderer::addModel( gl::VboMesh& VBOMeshRef, const DeferredMaterial mat, const BOOL isShadowsCaster, const Matrix44f modelMatrix  )
{
    DeferredModel *model = new DeferredModel();
    model->setup( VBOMeshRef, mat, isShadowsCaster, modelMatrix );
    mModels.push_back( model );
    return model;
}

void DeferredRenderer::prepareDeferredScene()
{
    glClearColor( 0.5f, 0.5f, 0.5f, 1 );
    glClearDepth(1.0f);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    
    //update ortho cam for full screen quads
    mOrthoCam.lookAt(mCam->getEyePoint(), mCam->getCenterOfInterestPoint());
    mOrthoCam.setCenterOfInterestPoint( mCam->getCenterOfInterestPoint() );
    mOrthoCam.setWorldUp(Vec3f(0.0f, 1.0f, 0.0f));
    
    renderDeferredFBO();
    
    if( mDeferFlags & SHADOWS_ENABLED_FLAG ) {
        createShadowMaps();
        renderShadowsToFBOs();
    }
    
    renderLightsToFBO();
    
    if( mDeferFlags & SSAO_ENABLED_FLAG ) {
        renderSSAOToFBO();
    }
}

void DeferredRenderer::createShadowMaps()
{
    glViewport(0, 0, mShadowMapRes, mShadowMapRes);
    
    //render depth map cube
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    for(vector<Light_Point*>::iterator currPointLight = mPointLights.begin(); currPointLight != mPointLights.end(); ++currPointLight)
    {
        if ( !(*currPointLight)->doesCastShadows() ) {
            continue;
        }
        
        (*currPointLight)->mDepthFBO.bindFramebuffer();
        glClearDepth(1.0f);
        glClear( GL_DEPTH_BUFFER_BIT );
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        
        mDepthWriteShader.bind();
        mDepthWriteShader.uniform("projection_mat", (*currPointLight)->mShadowCam.getProjectionMatrix());
        for (size_t i = 0; i < 6; ++i) {
            (*currPointLight)->mShadowMap.bindDepthFB( i );
            glClear(GL_DEPTH_BUFFER_BIT);
            mDepthWriteShader.uniform("modelview_mat", mLightFaceViewMatrices[i] * (*currPointLight)->mShadowCam.getModelViewMatrix());
            drawModels( SHADER_TYPE_DEPTH, &mDepthWriteShader, true );
        }
        mDepthWriteShader.unbind();
        
        (*currPointLight)->mDepthFBO.unbindFramebuffer();
    }
    
    //render spot light
    for(vector<Light_Spot*>::iterator currSpotLight = mSpotLights.begin(); currSpotLight != mSpotLights.end(); ++currSpotLight)
    {
        if (!(*currSpotLight)->doesCastShadows()) {
            continue;
        }
        
//        gl::enableDepthWrite();
//        (*currSpotLight)->mDepthFBO.bindFramebuffer();
//        glClear( GL_DEPTH_BUFFER_BIT );
////            glDrawBuffer(GL_NONE);
////            glReadBuffer(GL_NONE);
//        gl::setMatrices((*currSpotLight)->mShadowCam);
//        
//        if (drawShadowCasters) {
//            drawShadowCasters( SHADER_TYPE_NONE, NULL );
//        }
//        
//        (*currSpotLight)->mDepthFBO.unbindFramebuffer();
        
        glViewport(0, 0, mShadowMapRes, mShadowMapRes);
        mDepthWriteShader.bind();
        mDepthWriteShader.uniform("projection_mat", (*currSpotLight)->mShadowCam.getProjectionMatrix());
        (*currSpotLight)->mDepthFBO.bindFramebuffer();
        glClear(GL_DEPTH_BUFFER_BIT);
        mDepthWriteShader.uniform("modelview_mat", (*currSpotLight)->mShadowCam.getModelViewMatrix());
        drawModels( SHADER_TYPE_DEPTH, &mDepthWriteShader, true );
        mDepthWriteShader.unbind();
    }
    
    glDisable(GL_CULL_FACE);
}

void DeferredRenderer::renderShadowsToFBOs()
{
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_CUBE_MAP);
    for(vector<Light_Point*>::iterator currPointLight = mPointLights.begin(); currPointLight != mPointLights.end(); ++currPointLight) {
        if (!(*currPointLight)->doesCastShadows()) {
            continue;
        }
        
        (*currPointLight)->mShadowsFbo.bindFramebuffer();
        glClearColor( 0.5f, 0.5f, 0.5f, 0.0f );
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        gl::setViewport( (*currPointLight)->mShadowsFbo.getBounds() );
        glCullFace(GL_BACK); //don't need what we won't see
        //gl::setMatrices( *mCam );
        
        mPointShadowShader.bind();
        (*currPointLight)->mShadowMap.bind(0); //the magic texture
        mPointShadowShader.uniform( "shadow", 0);
        //mPointShadowShader.uniform( "modelview_mat", mCam->getModelViewMatrix() );
        mPointShadowShader.uniform( "projection_mat", mCam->getProjectionMatrix() );
        mPointShadowShader.uniform( "light_pos", mCam->getModelViewMatrix().transformPointAffine( (*currPointLight)->mShadowCam.getEyePoint() )); //conversion from world-space to camera-space (required here)
        mPointShadowShader.uniform( "camera_modelview_mat_inv", mCam->getInverseModelViewMatrix()); //!! need to account for model_mat ...
        mPointShadowShader.uniform( "light_modelview_mat", (*currPointLight)->mShadowCam.getModelViewMatrix());
        mPointShadowShader.uniform( "light_projection_mat", (*currPointLight)->mShadowCam.getProjectionMatrix());
        drawScene( SHADER_TYPE_SHADOW, &mPointShadowShader );
        (*currPointLight)->mShadowMap.unbind();
        mPointShadowShader.unbind();
        
        (*currPointLight)->mShadowsFbo.unbindFramebuffer();
    }
    glDisable(GL_TEXTURE_CUBE_MAP);
    
    for(vector<Light_Spot*>::iterator currSpotLight = mSpotLights.begin(); currSpotLight != mSpotLights.end(); ++currSpotLight) {
        if (!(*currSpotLight)->doesCastShadows()) {
            continue;
        }
        
        (*currSpotLight)->mShadowsFbo.bindFramebuffer();
        glClearColor( 0.5f, 0.5f, 0.5f, 0.0f );
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        gl::setViewport( (*currSpotLight)->mShadowsFbo.getBounds() );
        glCullFace(GL_BACK); //don't need what we won't see
        //gl::setMatrices( *mCam );
        
        //get shadow matrix transform
        Matrix44f shadowTransMatrix = (*currSpotLight)->mShadowCam.getProjectionMatrix();
        shadowTransMatrix *= (*currSpotLight)->mShadowCam.getModelViewMatrix();
        shadowTransMatrix *= mCam->getInverseModelViewMatrix();

        (*currSpotLight)->mDepthFBO.bindDepthTexture(0);
        mSpotShadowShader.bind();
        mSpotShadowShader.uniform( "depthTexture", 0 );
        mSpotShadowShader.uniform( "shadowTransMatrix", shadowTransMatrix );
        mSpotShadowShader.uniform( "projection_mat", mCam->getProjectionMatrix() );
        drawScene( SHADER_TYPE_SHADOW, &mSpotShadowShader );
        mSpotShadowShader.unbind();
        (*currSpotLight)->mDepthFBO.unbindTexture();
        (*currSpotLight)->mShadowsFbo.unbindFramebuffer();
    }
    glDisable(GL_CULL_FACE);
    
    //render all shadow layers to one FBO
    mAllShadowsFBO.bindFramebuffer();
    gl::setViewport( mAllShadowsFBO.getBounds() );
    gl::setMatricesWindow( mAllShadowsFBO.getSize() ); //want textures to fill FBO
    glClearColor( 0.5f, 0.5f, 0.5f, 0.0 );
    glClearDepth(1.0f);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    gl::enableAlphaBlending();
    
    //draw point light shadows
    for(vector<Light_Point*>::iterator currPointLight = mPointLights.begin(); currPointLight != mPointLights.end(); ++currPointLight) {
        if ( !(*currPointLight)->doesCastShadows() ) {
            continue;
        }
        
        (*currPointLight)->mShadowsFbo.getTexture().bind();
        gl::drawSolidRect( Rectf( 0, (float)mAllShadowsFBO.getHeight(), (float)mAllShadowsFBO.getWidth(), 0) ); //this is different as we are not using shaders to color these quads (need to fit viewport)
        (*currPointLight)->mShadowsFbo.getTexture().unbind();
    }
    
    //draw spot light shadows
    for(vector<Light_Spot*>::iterator currSpotLight = mSpotLights.begin(); currSpotLight != mSpotLights.end(); ++currSpotLight) {
        if ( !(*currSpotLight)->doesCastShadows() ) {
            continue;
        }
        
        (*currSpotLight)->mShadowsFbo.getTexture().bind();
        gl::drawSolidRect( Rectf( 0, (float)mAllShadowsFBO.getHeight(), (float)mAllShadowsFBO.getWidth(), 0) ); //this is different as we are not using shaders to color these quads (need to fit viewport)
        (*currSpotLight)->mShadowsFbo.getTexture().unbind();
    }
    
    gl::disableAlphaBlending();
    mAllShadowsFBO.unbindFramebuffer();
}

void DeferredRenderer::renderDeferredFBO()
{
    //render out main scene to FBO
    mDeferredFBO.bindFramebuffer();
    gl::setViewport( mDeferredFBO.getBounds() );
    
    glClearColor( 0.5f, 0.5f, 0.5f, 1 );
    glClearDepth(1.0f);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    
    mDeferredShader.bind();
    mDeferredShader.uniform("projectionMatrix", mCam->getProjectionMatrix());
    mDeferredShader.uniform("diffuse", Vec3f(1.0f, 1.0f, 1.0f));
    mDeferredShader.uniform("specular", Vec3f(1.0f, 1.0f, 1.0f));
    mDeferredShader.uniform("emissive", Vec3f(1.0f, 1.0f, 1.0f));
    mDeferredShader.uniform("shininess", 10.0f);
    mDeferredShader.uniform("additiveSpecular", 10.0f);
    
    Matrix33f normalMatrix = mCam->getModelViewMatrix().subMatrix33(0, 0);
    normalMatrix.invert();
    normalMatrix.transpose();
    mDeferredShader.uniform("normalMatrix", normalMatrix ); //getInverse( object modelMatrix ).transpose()
    
    //render light meshes if visible
    drawLightPointMeshes( SHADER_TYPE_DEFERRED, &mDeferredShader );
    drawLightSpotMeshes( SHADER_TYPE_DEFERRED, &mDeferredShader );
    
    mDeferredShader.uniform("modelViewMatrix", mCam->getModelViewMatrix());
    
    //now draw models ... TODO: might want to combine into one function/loop call
    drawModels( SHADER_TYPE_DEFERRED, &mDeferredShader, true);
    drawModels( SHADER_TYPE_DEFERRED, &mDeferredShader, false);
    
    mDeferredShader.unbind();
    mDeferredFBO.unbindFramebuffer();
}

void DeferredRenderer::renderSSAOToFBO()
{
    //render out main scene to FBO
    mSSAOMap.bindFramebuffer();
    gl::setViewport( mSSAOMap.getBounds() );
    gl::setMatricesWindow( mSSAOMap.getSize() ); //setting orthogonal view as rendering to a fullscreen quad
    
    glClearColor( 0.5f, 0.5f, 0.5f, 1 );
    glClearDepth(1.0f);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    
    mRandomNoise.bind(0);
    mDeferredFBO.getTexture(1).bind(1);
    mSSAOShader.bind();
    mSSAOShader.uniform("rnm", 0 );
    mSSAOShader.uniform("normalMap", 1 );
    
    gl::drawSolidRect( Rectf( 0.0f, 0.0f, (float)mSSAOMap.getWidth(), (float)mSSAOMap.getHeight()) );
    
    mSSAOShader.unbind();
    
    mDeferredFBO.getTexture(1).unbind(1);
    mRandomNoise.unbind(0);
    
    mSSAOMap.unbindFramebuffer();
}

void DeferredRenderer::renderLightsToFBO()
{
    mLightGlowFBO.bindFramebuffer();
    gl::setViewport( mLightGlowFBO.getBounds() );
    glClearColor( 0.0f, 0.0f, 0.0f, 1.0 );
    glClearDepth(1.0f);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    //draw light effects
    renderLights();
    mLightGlowFBO.unbindFramebuffer();
}

void DeferredRenderer::pingPongBlurSSAO()
{
    //--------- render horizontal blur first --------------
    mPingPongBlurH.bindFramebuffer();
    gl::setViewport( mPingPongBlurH.getBounds() );
    gl::setMatricesWindow( mPingPongBlurH.getSize() );
    glClearColor( 0.5f, 0.5f, 0.5f, 1 );
    glClearDepth(1.0f);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    
    mSSAOMap.getTexture().bind(0);
    mHBlurShader.bind();
    mHBlurShader.uniform("RTScene", 0);
    mHBlurShader.uniform("blurStep", 1.0f/mPingPongBlurH.getWidth()); //so that every "blur step" will equal one pixel width of this FBO
    gl::drawSolidRect( Rectf( 0.0f, 0.0f, (float)mPingPongBlurH.getWidth(), (float)mPingPongBlurH.getHeight()) );
    mHBlurShader.unbind();
    mSSAOMap.getTexture().unbind(0);
    mPingPongBlurH.unbindFramebuffer();
    
    //--------- now render vertical blur --------------
    mPingPongBlurV.bindFramebuffer();
    gl::setViewport( mPingPongBlurV.getBounds() );
    gl::setMatricesWindow( mPingPongBlurV.getSize() );
    glClearColor( 0.5f, 0.5f, 0.5f, 1 );
    glClearDepth(1.0f);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    
    mPingPongBlurH.getTexture().bind(0);
    mHBlurShader.bind();
    mHBlurShader.uniform("RTBlurH", 0);
    mHBlurShader.uniform("blurStep", 1.0f/mPingPongBlurH.getHeight()); //so that every "blur step" will equal one pixel height of this FBO
    gl::drawSolidRect( Rectf( 0.0f, 0.0f, (float)mPingPongBlurH.getWidth(), (float)mPingPongBlurH.getHeight()) );
    mHBlurShader.unbind();
    mPingPongBlurH.getTexture().unbind(0);
    mPingPongBlurV.unbindFramebuffer();
}

void DeferredRenderer::renderFullScreenQuad( const int renderType, const BOOL autoPrepareScene )
{
    renderQuad( renderType, Rectf( 0.0f, (float)getWindowHeight(), (float)getWindowWidth(), 0.0f), autoPrepareScene );
}

void DeferredRenderer::renderQuad( const int renderType, Rectf renderQuad, const BOOL autoPrepareScene )
{
    if( autoPrepareScene ) {
        prepareDeferredScene(); //prepare all FBOs for use
    }
    
    switch (renderType)
    {
        case SHOW_FINAL_VIEW: {
            
            if( mDeferFlags & SSAO_ENABLED_FLAG ) {
                pingPongBlurSSAO();
            }
            
            if(fRenderOverlayFunc || fRenderParticlesFunc) {
                glDisable(GL_DEPTH_TEST);
            }
            
            if(fRenderOverlayFunc) {
                gl::enableAlphaBlending();
                
                mOverlayFBO.bindFramebuffer();
                glClearColor( 0.5f, 0.5f, 0.5f, 0.0f );
                glClearDepth(1.0f);
                glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
                gl::setMatrices(*mCam);
                gl::setViewport( mOverlayFBO.getBounds() );
                
                fRenderOverlayFunc();
                
                mOverlayFBO.unbindFramebuffer();
            }
            
            if(fRenderParticlesFunc) {
                //copy deferred depth buffer to "particles" FBO so we can have transparency + occlusion
                glEnable(GL_DEPTH_TEST);    //want to read depth buffer
                glDepthMask(GL_TRUE);       //want to write to depth buffer
                glBindFramebuffer(GL_READ_FRAMEBUFFER, mDeferredFBO.getId());
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mParticlesFBO.getId());
                
                //glReadBuffer(GL_DEPTH_ATTACHMENT);
                //glDrawBuffer(GL_DEPTH_ATTACHMENT);
                
                glBlitFramebuffer(  0, 0, mDeferredFBO.getWidth(), mDeferredFBO.getHeight(),
                                  0, 0, mParticlesFBO.getWidth(), mParticlesFBO.getHeight(),
                                  GL_DEPTH_BUFFER_BIT, GL_NEAREST );
                
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                
                //draw to FBO
                glDepthMask(GL_FALSE);  //don't want to write to depth buffer / prevent overriding
                mParticlesFBO.bindFramebuffer();
                glClearColor( 0.5f, 0.5f, 0.5f, 0.0f );
                glClear( GL_COLOR_BUFFER_BIT );
                gl::setMatrices(*mCam);
                gl::setViewport( mParticlesFBO.getBounds() );
                
                fRenderParticlesFunc();
                
                mParticlesFBO.unbindFramebuffer();
                glDepthMask(GL_TRUE);
            }
            
            if(fRenderOverlayFunc || fRenderParticlesFunc) {
                glEnable(GL_DEPTH_TEST);
                gl::disableAlphaBlending();
            }
            
            mFinalSSFBO.bindFramebuffer();
            glClearColor( 0.5f, 0.5f, 0.5f, 1 );
            glClearDepth(1.0f);
            glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
            gl::setViewport( mFinalSSFBO.getBounds() );
            gl::setMatricesWindow( mFinalSSFBO.getSize() );
            if( mDeferFlags & SSAO_ENABLED_FLAG ) {
                mPingPongBlurV.getTexture().bind(0);
            }
            if( mDeferFlags & SHADOWS_ENABLED_FLAG ) {
                mAllShadowsFBO.getTexture().bind(1);
            }
            mLightGlowFBO.getTexture().bind(2);
            mBasicBlender.bind();
            mBasicBlender.uniform("ssaoTex", 0 );
            mBasicBlender.uniform("shadowsTex", 1 );
            mBasicBlender.uniform("baseTex", 2 );
            mBasicBlender.uniform("useSSAO", (mDeferFlags & SSAO_ENABLED_FLAG) );
            mBasicBlender.uniform("useShadows", (mDeferFlags & SHADOWS_ENABLED_FLAG) );
            mBasicBlender.uniform("onlySSAO", false );
            gl::drawSolidRect( Rectf( 0.0f, (float)mFinalSSFBO.getHeight(), (float)mFinalSSFBO.getWidth(), 0.0f) );
            mBasicBlender.unbind();
            mLightGlowFBO.getTexture().unbind(2);
            if( mDeferFlags & SHADOWS_ENABLED_FLAG ) {
                mAllShadowsFBO.getTexture().unbind(1);
            }
            if( mDeferFlags & SSAO_ENABLED_FLAG ) {
                mPingPongBlurV.getTexture().unbind(0);
            }
            
            if(fRenderOverlayFunc || fRenderParticlesFunc) {
                gl::enableAlphaBlending();
            }
            
            if(fRenderParticlesFunc) {
                mParticlesFBO.getTexture().bind(0);
                gl::drawSolidRect( Rectf( 0.0f, (float)mFinalSSFBO.getHeight(), (float)mFinalSSFBO.getWidth(), 0.0f) );
                mParticlesFBO.getTexture().unbind(0);
            }
            
            if(fRenderOverlayFunc) {
                mOverlayFBO.getTexture().bind();
                gl::drawSolidRect( Rectf( 0.0f, (float)mFinalSSFBO.getHeight(), (float)mFinalSSFBO.getWidth(), 0.0f) );
                mOverlayFBO.getTexture().unbind();
            }
            
            if(fRenderOverlayFunc || fRenderParticlesFunc) {
                gl::disableAlphaBlending();
            }
            
            mFinalSSFBO.unbindFramebuffer();
            
            gl::setViewport( getWindowBounds() );
            gl::setMatricesWindow( getWindowSize() ); //want textures to fill screen
            mFinalSSFBO.getTexture().bind(0);
            
            if( (mDeferFlags & FXAA_ENABLED_FLAG) && !(mDeferFlags & (MSAA_4X_ENABLED_FLAG | MSAA_8X_ENABLED_FLAG | MSAA_16X_ENABLED_FLAG)) ) {
                mFXAAShader.bind();
                mFXAAShader.uniform("buf0", 0);
                mFXAAShader.uniform("frameBufSize", Vec2f((float)mFinalSSFBO.getWidth(), (float)mFinalSSFBO.getHeight()));
            }
            gl::drawSolidRect( renderQuad );
            if( (mDeferFlags & FXAA_ENABLED_FLAG) && !(mDeferFlags & (MSAA_4X_ENABLED_FLAG | MSAA_8X_ENABLED_FLAG | MSAA_16X_ENABLED_FLAG)) ) {
                mFXAAShader.unbind();
            }
            mFinalSSFBO.getTexture().unbind(0);
        }
            break;
        case SHOW_DIFFUSE_VIEW: {
            gl::setViewport( getWindowBounds() );
            gl::setMatricesWindow( getWindowSize() ); //want textures to fill screen
            mDeferredFBO.getTexture(0).bind(0);
            gl::drawSolidRect( renderQuad );
            mDeferredFBO.getTexture(0).unbind(0);
        }
            break;
        case SHOW_DEPTH_VIEW: {
            gl::setViewport( getWindowBounds() );
            gl::setMatricesWindow( getWindowSize() ); //want textures to fill screen
            mDeferredFBO.getTexture(1).bind(0);
            mAlphaToRBG.bind();
            mAlphaToRBG.uniform("alphaTex", 0);
            gl::drawSolidRect( renderQuad );
            mAlphaToRBG.unbind();
            mDeferredFBO.getTexture(1).unbind(0);
        }
            break;
        case SHOW_NORMALMAP_VIEW: {
            gl::setViewport( getWindowBounds() );
            gl::setMatricesWindow( getWindowSize() ); //want textures to fill screen
            mDeferredFBO.getTexture(1).bind(0);
            gl::drawSolidRect( renderQuad );
            mDeferredFBO.getTexture(1).unbind(0);
        }
            break;
        case SHOW_SSAO_VIEW: {
            if( mDeferFlags & SSAO_ENABLED_FLAG ) {
                gl::setViewport( getWindowBounds() );
                gl::setMatricesWindow( getWindowSize() ); //want textures to fill screen
                mSSAOMap.getTexture().bind(0);
                mBasicBlender.bind();
                mBasicBlender.uniform("ssaoTex", 0 );
                mBasicBlender.uniform("shadowsTex", 0 );    //just binding same one so only it shows....
                mBasicBlender.uniform("baseTex", 0 );       //just binding same one so only it shows....
                mBasicBlender.uniform("useSSAO", true );
                mBasicBlender.uniform("useShadows", false );
                mBasicBlender.uniform("onlySSAO", true );
                gl::drawSolidRect( renderQuad );
                mBasicBlender.unbind();
                mSSAOMap.getTexture().unbind(0);
            }
        }
            break;
        case SHOW_SSAO_BLURRED_VIEW: {
            if( mDeferFlags & SSAO_ENABLED_FLAG ) {
                pingPongBlurSSAO();
                
                gl::setViewport( getWindowBounds() );
                gl::setMatricesWindow( getWindowSize() ); //want textures to fill screen
                mPingPongBlurV.getTexture().bind(0);
                gl::drawSolidRect( renderQuad );
                mPingPongBlurV.getTexture().unbind(0);
            }
        }
            break;
        case SHOW_LIGHT_VIEW: {
            gl::setViewport( getWindowBounds() );
            gl::setMatricesWindow( getWindowSize() ); //want textures to fill screen
            mLightGlowFBO.getTexture().bind(0);
            gl::drawSolidRect( renderQuad );
            mLightGlowFBO.getTexture().unbind(0);
        }
            break;
        case SHOW_SHADOWS_VIEW: {
            if ( mDeferFlags & SHADOWS_ENABLED_FLAG ) {
                gl::setViewport( getWindowBounds() );
                gl::setMatricesWindow( getWindowSize() ); //want textures to fill screen
                mAllShadowsFBO.getTexture().bind(0);
                gl::drawSolidRect( renderQuad );
                mAllShadowsFBO.getTexture().unbind(0);
            }
        }
            break;
    }
}

void DeferredRenderer::drawLightPointMeshes( int shaderType, gl::GlslProg* shader )
{
    //point lights
    for(vector<Light_Point*>::iterator currLight = mPointLights.begin(); currLight != mPointLights.end(); ++currLight) {
        if ( shader ) {
            switch (shaderType) {
                case SHADER_TYPE_DEFERRED: {
                    shader->uniform("modelViewMatrix", mCam->getModelViewMatrix() * (*currLight)->modelMatrix);
                    (*currLight)->renderProxy(); //render the proxy shape
                }
                    break;
                case SHADER_TYPE_LIGHT: {
                    shader->uniform("light_col", (*currLight)->getColor());
                    shader->uniform("light_radius", (*currLight)->getLightMaskRadius());
                    shader->uniform("light_intensity", (*currLight)->getIntensity());
                    shader->uniform("light_pos_vs", mCam->getModelViewMatrix().transformPointAffine( (*currLight)->getPos() ));
                    shader->uniform("modelview_mat", mCam->getModelViewMatrix() * (*currLight)->modelMatrixAOE );
                    (*currLight)->renderProxyAOE(); //render the proxy shape
                }
                    break;
                case SHADER_TYPE_SHADOW: {
                    //nothing to render
                    break;
                }
                default:
                    console() << "warning: no shader type selected \n";
                    break;
            }
        }
    }
}

void DeferredRenderer::drawLightSpotMeshes( int shaderType, gl::GlslProg* shader )
{
    for(vector<Light_Spot*>::iterator currLight = mSpotLights.begin(); currLight != mSpotLights.end(); ++currLight) {
        if ( shader ) {
            switch (shaderType) {
                case SHADER_TYPE_DEFERRED: {
                    shader->uniform("modelViewMatrix", mCam->getModelViewMatrix() * (*currLight)->modelMatrix);
                    //(*currLight)->renderProxy(); //!!!!!render the proxy shape - needs to be cone shape
                }
                    break;
                case SHADER_TYPE_LIGHT: {
                    Vec3f eyePt = mOrthoCam.getModelViewMatrix().transformPoint( (*currLight)->getPos() );
                    Vec3f targetPt = mOrthoCam.getModelViewMatrix().transformPoint( (*currLight)->getTarget() );
                    Vec3f lightDir = eyePt - targetPt;
                
                    shader->uniform("light_angle", (*currLight)->getLightAngle());// (*currLight)->getLightAngle());
                    shader->uniform("light_col", (*currLight)->getColor());
                    shader->uniform("light_intensity", (*currLight)->getIntensity());
                    shader->uniform("light_pos_vs", mCam->getModelViewMatrix().transformPoint( (*currLight)->getPos() ));
                    shader->uniform("light_dir_vs", lightDir.normalized() );
                    gl::draw( mFSQuadVBOMesh );
                }
                    break;
                case SHADER_TYPE_SHADOW: {
                    //nothing to render
                    break;
                }
                default:
                    console() << "warning: no shader type selected \n";
                    break;
            }
        }
    }
}

void DeferredRenderer::drawScene( int shaderType, gl::GlslProg *shader )
{
    drawModels( shaderType, shader, true );
    drawModels( shaderType, shader, false);
    
    drawLightPointMeshes( shaderType, shader ); //!!need to relook at this for shadow-mapping as I need to pass matrices
    drawLightSpotMeshes( shaderType, shader ); //!!need to relook at this for shadow-mapping as I need to pass matrices
}

void DeferredRenderer::renderLights()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); //set blend function
    glEnable(GL_CULL_FACE); //cull front faces
    glCullFace(GL_FRONT);
    glDisable(GL_DEPTH_TEST); //disable depth testing
    glDepthMask(false);
    
    //point lights
    mDeferredFBO.getTexture(0).bind(0); //bind color tex
    mDeferredFBO.getTexture(1).bind(1); //bind normal/depth tex
    
    mLightPointShader.bind(); //bind point light pixel shader
    mLightPointShader.uniform("projection_mat", mCam->getProjectionMatrix());
    mLightPointShader.uniform("sampler_col", 0);
    mLightPointShader.uniform("sampler_normal_depth", 1);
    mLightPointShader.uniform("proj_inv_mat", mCam->getProjectionMatrix().inverted());
    mLightPointShader.uniform("view_height", mDeferredFBO.getHeight());
    mLightPointShader.uniform("view_width", mDeferredFBO.getWidth());
    drawLightPointMeshes( SHADER_TYPE_LIGHT, &mLightPointShader );
    mLightPointShader.unbind(); //unbind and reset everything to desired values
    
    mLightSpotShader.bind(); //bind point light pixel shader
    mLightSpotShader.uniform("sampler_col", 0);
    mLightSpotShader.uniform("sampler_normal_depth", 1);
    mLightSpotShader.uniform("proj_inv_mat", mCam->getProjectionMatrix().inverted());
    mLightSpotShader.uniform("view_height", (float)mDeferredFBO.getHeight());
    mLightSpotShader.uniform("view_width", (float)mDeferredFBO.getWidth());
    drawLightSpotMeshes( SHADER_TYPE_LIGHT, &mLightSpotShader );
    mLightSpotShader.unbind(); //unbind and reset everything to desired values
    
    mDeferredFBO.getTexture(0).unbind(0); //bind color tex
    mDeferredFBO.getTexture(1).unbind(1); //bind normal/depth tex
    
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(true);
    glDisable(GL_BLEND);
}

void DeferredRenderer::initTextures()
{
    mRandomNoise = gl::Texture( loadImage( loadResource( RES_TEX_NOISE_SAMPLER ) ) ); //noise texture required for SSAO calculations
}

void DeferredRenderer::initShaders()
{
    mDeferredShader     = gl::GlslProg( loadResource( RES_GLSL_DEFER_VERT ), loadResource( RES_GLSL_DEFER_FRAG ) );
    mBasicBlender       = gl::GlslProg( loadResource( RES_GLSL_BASIC_BLENDER_VERT ), loadResource( RES_GLSL_BASIC_BLENDER_FRAG ) );
    
    if(mDeferFlags & SSAO_ENABLED_FLAG) {
        mSSAOShader		= gl::GlslProg( loadResource( RES_GLSL_SSAO_VERT ), loadResource( RES_GLSL_SSAO_FRAG ) );
    }
    mHBlurShader		= gl::GlslProg( loadResource( RES_GLSL_BLUR_H_VERT ), loadResource( RES_GLSL_BLUR_H_FRAG ) );
    mVBlurShader		= gl::GlslProg( loadResource( RES_GLSL_BLUR_V_VERT ), loadResource( RES_GLSL_BLUR_V_FRAG ) );
    
    mLightPointShader	= gl::GlslProg( loadResource( RES_GLSL_LIGHT_POINT_VERT ), loadResource( RES_GLSL_LIGHT_POINT_FRAG ) );
    mLightSpotShader	= gl::GlslProg( loadResource( RES_GLSL_LIGHT_SPOT_VERT ), loadResource( RES_GLSL_LIGHT_SPOT_FRAG ) );
    mAlphaToRBG         = gl::GlslProg( loadResource( RES_GLSL_ALPHA_RGB_VERT ), loadResource( RES_GLSL_ALPHA_RGB_FRAG ) );
    
    mPointShadowShader  = gl::GlslProg( loadResource( RES_GLSL_POINTSHADOW_VERT ), loadResource( RES_GLSL_POINTSHADOW_FRAG ) );
    mSpotShadowShader   = gl::GlslProg( loadResource( RES_GLSL_SPOTSHADOW_VERT ), loadResource( RES_GLSL_SPOTSHADOW_FRAG ) );
    mDepthWriteShader   = gl::GlslProg( loadResource( RES_GLSL_DEPTHWRITE_VERT ), loadResource( RES_GLSL_DEPTHWRITE_FRAG ) );
    mBasicShader        = gl::GlslProg( loadResource( RES_GLSL_BASIC_VERT ), loadResource( RES_GLSL_BASIC_FRAG ) );
    
    if( (mDeferFlags & FXAA_ENABLED_FLAG) && !(mDeferFlags & (MSAA_4X_ENABLED_FLAG | MSAA_8X_ENABLED_FLAG | MSAA_16X_ENABLED_FLAG)) ) {
        mFXAAShader		= gl::GlslProg( loadResource( RES_GLSL_FXAA_VERT ), loadResource( RES_GLSL_FXAA_FRAG ) );
    }
}

void DeferredRenderer::initFBOs()
{
    //this FBO will capture normals, depth, and base diffuse in one render pass (as opposed to three)
    gl::Fbo::Format deferredFBO;
    deferredFBO.enableDepthBuffer();
    deferredFBO.setDepthInternalFormat( GL_DEPTH_COMPONENT24 ); //want fbo to have precision depth map as well
    deferredFBO.setColorInternalFormat( GL_RGBA32F_ARB );
    deferredFBO.enableColorBuffer( true, 2 ); // create an FBO with two color attachments (basic diffuse and normal/depth view, get position from depth)
    
    if (mDeferFlags & MSAA_16X_ENABLED_FLAG) {
        deferredFBO.setSamples(16); //only deferred shader for now
    }
    else if (mDeferFlags & MSAA_8X_ENABLED_FLAG) {
        deferredFBO.setSamples(8); //only deferred shader for now
    }
    else if (mDeferFlags & MSAA_4X_ENABLED_FLAG) {
        deferredFBO.setSamples(4); //only deferred shader for now
    }
    
    gl::Fbo::Format basicFormat;
    basicFormat.enableDepthBuffer(false); //don't need depth "layers"
    
    gl::Fbo::Format basicDepthFormat;
    basicDepthFormat.enableDepthBuffer(true); //don't need depth "layers"
    basicDepthFormat.setDepthInternalFormat( GL_DEPTH_COMPONENT24 ); //want fbo to have same precision as deferred
    
    //init screen space render
    mDeferredFBO	= gl::Fbo( mFBOResolution.x,    mFBOResolution.y,   deferredFBO );
    mLightGlowFBO   = gl::Fbo( mFBOResolution.x,    mFBOResolution.y,   basicFormat );
    
    mPingPongBlurH	= gl::Fbo( mFBOResolution.x/2,  mFBOResolution.y/2, basicFormat ); //don't need as high res on ssao as it will be blurred anyhow ...
    mPingPongBlurV	= gl::Fbo( mFBOResolution.x/2,  mFBOResolution.y/2, basicFormat );
    mSSAOMap		= gl::Fbo( mFBOResolution.x/2,  mFBOResolution.y/2, basicFormat );

    if(mDeferFlags & SHADOWS_ENABLED_FLAG) {
        mAllShadowsFBO  = gl::Fbo( mFBOResolution.x,    mFBOResolution.y,   basicFormat );
    }

    if ( fRenderOverlayFunc ) {
        mOverlayFBO     = gl::Fbo( mFBOResolution.x,    mFBOResolution.y,   basicFormat );
    }
    
    if ( fRenderParticlesFunc ) {
        mParticlesFBO   = gl::Fbo( mFBOResolution.x,    mFBOResolution.y,   basicDepthFormat );
    }

    mFinalSSFBO		= gl::Fbo( mFBOResolution.x,    mFBOResolution.y,   basicFormat );
    
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
}

#pragma mark - draw functions . All drawing now doen by adding "models"

void DeferredRenderer::drawModels(int shaderType, gl::GlslProg* shader, BOOL drawShadowCasters)
{
    if ( shader ) {
        switch (shaderType) {
            case DeferredRenderer::SHADER_TYPE_DEFERRED: {
                
                for ( vector<DeferredModel*>::iterator model = mModels.begin(); model != mModels.end(); model++ ) {
                    
                    //only want to draw which type sof models function is asking for
                    if(drawShadowCasters != (*model)->_isShadowCaster) {
                        continue;
                    }
                    
                    if( (*model)->diffuseTex ) {
                        (*model)->diffuseTex->bind(0);
                        mDeferredShader.uniform("useDiffuseTex", 1.0f);
                        mDeferredShader.uniform("texDiffuse", 0);
                    }
                    else {
                        mDeferredShader.uniform("useDiffuseTex", 0.0f);
                    }
                    
                    shader->uniform("diffuse", (*model)->material.diffuseCol );
                    shader->uniform("specular", (*model)->material.specularCol );
                    shader->uniform("emissive", (*model)->material.emissiveCol );
                    shader->uniform("shininess", (*model)->material.shininess );
                    shader->uniform("additiveSpecular", (*model)->material.additiveSpecular );
                    shader->uniform("modelViewMatrix", mCam->getModelViewMatrix() * (*model)->getModelMatrix());
                    (*model)->render();
                    
                    if( (*model)->diffuseTex ) {
                        (*model)->diffuseTex->unbind(0);
                    }
                }
            }
                break;
            case DeferredRenderer::SHADER_TYPE_LIGHT: {
                for ( vector<DeferredModel*>::iterator model = mModels.begin(); model != mModels.end(); model++ ) {
                    
                    //only want to draw which type sof models function is asking for
                    if(drawShadowCasters != (*model)->_isShadowCaster) {
                        continue;
                    }
                    
                    shader->uniform("modelview_mat", mCam->getModelViewMatrix() * (*model)->getModelMatrix());
                    (*model)->render();
                }
                break;
            }
            case DeferredRenderer::SHADER_TYPE_SHADOW: {
                for ( vector<DeferredModel*>::iterator model = mModels.begin(); model != mModels.end(); model++ ) {
                    
                    //only want to draw which type sof models function is asking for
                    if(drawShadowCasters != (*model)->_isShadowCaster) {
                        continue;
                    }
                    
                    shader->uniform("modelview_mat", mCam->getModelViewMatrix() * (*model)->getModelMatrix());
                    shader->uniform("model_mat_inv", (*model)->getModelMatrix().inverted() );
                    (*model)->render();
                }
                break;
            }
            case DeferredRenderer::SHADER_TYPE_DEPTH: {
                for ( vector<DeferredModel*>::iterator model = mModels.begin(); model != mModels.end(); model++ ) {
                    
                    //only want to draw which type sof models function is asking for
                    if(drawShadowCasters != (*model)->_isShadowCaster) {
                        continue;
                    }
                    
                    shader->uniform("model_mat", (*model)->getModelMatrix() );
                    (*model)->render();
                }
                break;
            }
            case DeferredRenderer::SHADER_TYPE_NONE: {
                for ( vector<DeferredModel*>::iterator model = mModels.begin(); model != mModels.end(); model++ ) {
                    
                    //only want to draw which type sof models function is asking for
                    if(drawShadowCasters != (*model)->_isShadowCaster) {
                        continue;
                    }
                    
                    (*model)->render();
                }
                break;
            }
            default:
                break;
        }
    }
}