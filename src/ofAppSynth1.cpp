//
//  ofAppSynth1.cpp
//  fingerprint1
//
//  Created by Steve Meyfroidt on 06/06/2025.
//





/**
 TODO:
 - allow Mods to draw on multiple layers
 - recognise the difference between continuous parameter connections and events: is the auction idea relevant?
 - configure event responses here
 - genericise responding to a new continuous float for a parameter within some range
 */







#include "ofApp.h"

using namespace ofxMarkSynth;

std::shared_ptr<Synth> createSynth1(glm::vec2 size) {
//  auto synthPtr = std::make_shared<Synth>("Synth1", ModConfig {}, START_PAUSED, size);
//
//  // Audio source
//  std::shared_ptr<AudioDataSourceMod> audioDataSourceModPtr = std::static_pointer_cast<AudioDataSourceMod>(synthPtr->addMod<AudioDataSourceMod>("Audio Source", {
//    {"MinPitch", "50.0"},
//    {"MaxPitch", "600.0"},//"1500.0"},
//    {"MinRms", "0.0005"},
//    {"MaxRms", MAX_RMS},
//    {"MinComplexSpectralDifference", "200.0"},
//    {"MaxComplexSpectralDifference", "1000.0"},
//    {"MinSpectralCrest", "20.0"},
//    {"MaxSpectralCrest", "350.0"},
//    {"MinZeroCrossingRate", "5.0"},
//    {"MaxZeroCrossingRate", "15.0"}
//  }, MIC_DEVICE_NAME, RECORD_AUDIO, RECORDING_PATH, ROOT_SOURCE_MATERIAL_PATH));
//  //  audioDataSourceModPtr->registerAudioCallback([this](const float* audioBuffer, size_t bufferSize, int numChannels, int sampleRate) {
//  //    ofLogNotice() << audioBuffer[0];
//  //  });
//  
//  // Palette from raw spectral points
//  auto audioPaletteModPtr = synthPtr->addMod<SomPaletteMod>("Palette Creator", {
//    {"Iterations", "4000"}
//  });
//  
//  // Clusters from raw pitch/RMS points
//  auto clusterModPtr = synthPtr->addMod<ClusterMod>("Clusters", {
//    {"maxSourcePoints", "600"},
//    {"clusters", "7"}
//  });
//  
//  // Palette ParticleField
//  auto particleFieldModPtr = synthPtr->addMod<ParticleFieldMod>("Particle Field", {
//    {"velocityDamping", "0.996"},
//    {"forceMultiplier", "0.15"},
//    {"maxVelocity", "0.00015"},
//    {"particleSize", "12.0"},
//    {"jitterStrength", "0.5"},
//    {"ln2ParticleCount", "13.0"}
//  }, 0.0, 0.0);
//  
//  // Fluid simulation
//  auto fluidModPtr = synthPtr->addMod<FluidMod>("Fluid", {
//    {"dt", "0.007"},
//    {"value:dissipation", "0.999"},
//    {"velocity:dissipation", "0.998"},
//    {"vorticity", "50.0"},
//    {"value:iterations", "0.0"},
//    {"velocity:iterations", "0.0"},
//    {"pressure:iterations", "25.0"}
//  });
//  
//  // Clusters into fluid values
//  auto radiiModPtr = synthPtr->addMod<RandomFloatSourceMod>("Fluid Cluster Radius", {
//    {"CreatedPerUpdate", "0.05"},
//    {"Min", "0.005"},
//    {"Max", "0.02"}
//  }, std::pair<float, float>{0.0, 0.1}, std::pair<float, float>{0.0, 0.1});
//  
//  auto clusterPointsModPtr = synthPtr->addMod<SoftCircleMod>("Fluid Clusters", {
//    {"Radius Mean", "0.01"},
//    {"Radius Variance", "1.0"},
//    {"Radius Min", "0.0"},
//    {"Radius Max", "0.07"},
//    {"ColorMultiplier", "0.5"},
//    {"AlphaMultiplier", "0.3"},
//    {"Softness", "0.8"}
//  });
//  
//  // Radial fluid impulses from clusters
//  auto fluidRadialImpulseModPtr = synthPtr->addMod<FluidRadialImpulseMod>("Cluster Impulses", {
//    {"ImpulseRadius", "0.05"},
//    {"ImpulseStrength", "0.02"}
//  });
//  
//  // Raw data points into fluid
//  auto pitchRmsPointsModPtr = synthPtr->addMod<SoftCircleMod>("Fluid Raw Points", {
//    {"Radius Mean", "0.003"},
//    {"Radius Variance", "1.0"},
//    {"Radius Min", "0.0"},
//    {"Radius Max", "0.02"},
//    {"ColorMultiplier", "0.4"},
//    {"AlphaMultiplier", "0.6"},
//    {"Softness", "0.6"}
//  });
//  
//  // Radial fluid impulses from raw points
//  auto rawFluidRadialImpulseModPtr = synthPtr->addMod<FluidRadialImpulseMod>("Raw Point Impulses", {
//    {"ImpulseRadius", "0.025"},
//    {"ImpulseStrength", "0.08"}
//  });
//  
//  // Smeared raw data points
//  auto polarPitchRmsPointsModPtr = synthPtr->addMod<SoftCircleMod>("Raw Points", {
//    {"Radius Mean", "0.003"},
//    {"Radius Variance", "0.2"},
//    {"Radius Min", "0.0"},
//    {"Radius Max", "0.01"},
//    {"Softness", "0.5"},
//    {"AlphaMultiplier", "0.9"},
//    {"ColorMultiplier", "0.4"}
//  });
//  
//  auto smearModPtr = synthPtr->addMod<SmearMod>("Smear Raw Points", {
//    {"Translation", "0.0, 0.0"},
//    {"MixNew", "0.7"},
//    {"AlphaMultiplier", "0.997"},
//    {"Field1Multiplier", "0.02"},
//    {"Field1Bias", "0, 0"},
//    {"Field2Multiplier", "0.01"},
//    {"Field2Bias", "0, 0"},
//    //      {"FieldBias", "-0.5, -0.5"},
//  });
//  
//  // Collage layer from raw pitch/RMS and the raw points FBO
//  auto snapshotModPtr = synthPtr->addMod<PixelSnapshotMod>("Snapshot", {});
//  
//  auto pathModPtr = synthPtr->addMod<PathMod>("Collage Path", {
//    {"MaxVertices", "7"},
//    {"MinVertexProximity", "0.01"},
//    {"MaxVertexProximity", "0.07"},
//    {"Strategy", "0"} // 0=polypath; 1=bounds; 2=horizontals; 3=convex hull
//  });
//  
//  auto collageModPtr = synthPtr->addMod<CollageMod>("Collage", {
//    {"Saturation", "1.5"},
//    {"Strategy", "1"}, // 0=tint; 1=add tinted pixels; 2=add pixels
//  });
//  
//  // DividedArea
//  auto dividedAreaModPtr = synthPtr->addMod<DividedAreaMod>("Divided Area", {
//    {"maxConstrainedLines", "1500"},
//    {"constrainedWidth", "0.001"},
//    {"PathWidth", "0.003"}
//  });
//  
//  // Sandlines
//  auto sandLineModPtr = synthPtr->addMod<SandLineMod>("Sand lines", {
//    {"PointRadius", "1.0"},
//    {"Density", "0.15"},
//    {"AlphaMultiplier", "1.0"},
//    {"StdDevAlong", "0.5"},
//    {"StdDevPerpendicular", "0.004"}
//  });
//  
//  //  { // Cluster particles
//  //    auto particleSetModPtr = addMod<ParticleSetMod>(mods, "Cluster Particles", {
//  //      {"maxParticles", "500"},
//  //      {"maxParticleAge", "300"},
//  //      {"colourMultiplier", "0.5"}
//  //    });
//  //    clusterModPtr->addSink(ClusterMod::SOURCE_VEC2, particleSetModPtr, ParticleSetMod::SINK_POINTS);
//  //    audioPaletteModPtr->addSink(SomPaletteMod::SOURCE_RANDOM_LIGHT_VEC4, particleSetModPtr, DrawPointsMod::SINK_POINT_COLOR);
//  //
//  //    auto fadeModPtr = addMod<FadeMod>(mods, "Fade Particles", {
//  //      {"Fade Amount", "0.0005"}
//  //    });
//  //    particleSetModPtr->addSink(ParticleSetMod::SOURCE_FBO, fadeModPtr, FadeMod::SINK_FBO);
//  ////    auto multiplyModPtr = addMod<MultiplyMod>(mods, "Fade Cluster Particles", {
//  ////      {"Multiply By", "0.995"}
//  ////    });
//  ////    particleSetModPtr->addSink(ParticleSetMod::SOURCE_FBO, multiplyModPtr, MultiplyMod::SINK_FBO);
//  //
//  ////    particleSetModPtr->receive(ParticleSetMod::SINK_FBO, fluidFboPtr);
//  //    particleSetModPtr->receive(ParticleSetMod::SINK_FBO, fboClusterParticlesPtr);
//  //  }
//  
//  connectSourceToSinks(audioDataSourceModPtr, {
////    { AudioDataSourceMod::SOURCE_SPECTRAL_CREST_SCALAR, {
////      { polarPitchRmsPointsModPtr, SoftCircleMod::SINK_POINT_RADIUS_VARIANCE }
////    }},
//    { AudioDataSourceMod::SOURCE_SPECTRAL_3D_POINTS, {
//      { audioPaletteModPtr, SomPaletteMod::SINK_VEC3 }
//    }},
//    { AudioDataSourceMod::SOURCE_PITCH_SCALAR, {
//      { polarPitchRmsPointsModPtr, SoftCircleMod::SINK_COLOR_MULTIPLIER }
//    }},
//    { AudioDataSourceMod::SOURCE_POLAR_PITCH_RMS_POINTS, {
//      { clusterModPtr, ClusterMod::SINK_VEC2 },
//      { polarPitchRmsPointsModPtr, SoftCircleMod::SINK_POINTS },
//      { pathModPtr, PathMod::SINK_VEC2 },
//      { dividedAreaModPtr, DividedAreaMod::SINK_MINOR_ANCHORS }
//    }},
//    { AudioDataSourceMod::SOURCE_PITCH_RMS_POINTS, {
//      { pitchRmsPointsModPtr, SoftCircleMod::SINK_POINTS },
//      { rawFluidRadialImpulseModPtr, FluidRadialImpulseMod::SINK_POINTS }
//    }},
//    { AudioDataSourceMod::SOURCE_ONSET1, {
//      { clusterModPtr, ClusterMod::SINK_CHANGE_CLUSTER_NUM },
//      { smearModPtr, SmearMod::SINK_CHANGE_LAYER },
//      { polarPitchRmsPointsModPtr, SoftCircleMod::SINK_CHANGE_LAYER },
//      { clusterPointsModPtr, SoftCircleMod::SINK_CHANGE_LAYER },
//      { pitchRmsPointsModPtr, SoftCircleMod::SINK_CHANGE_LAYER },
//      { particleFieldModPtr, ParticleFieldMod::SINK_CHANGE_LAYER }
//    }},
//    { AudioDataSourceMod::SOURCE_TIMBRE_CHANGE, {
//      { audioPaletteModPtr, SomPaletteMod::SINK_SWITCH_PALETTE }
//    }},
//    { AudioDataSourceMod::SOURCE_PITCH_CHANGE, {
//    }},
//  });
//  connectSourceToSinks(audioPaletteModPtr, {
//    { SomPaletteMod::SOURCE_FIELD, {
//      { particleFieldModPtr, ParticleFieldMod::SINK_FIELD_1_FBO },
//      { smearModPtr, SmearMod::SINK_FIELD_1_FBO }
//    }},
//    { SomPaletteMod::SOURCE_RANDOM, {
//      { clusterPointsModPtr, SoftCircleMod::SINK_COLOR },
//      { particleFieldModPtr, ParticleFieldMod::SINK_POINT_COLOR }
//    }},
//    { SomPaletteMod::SOURCE_RANDOM_DARK, {
//      { pitchRmsPointsModPtr, SoftCircleMod::SINK_COLOR }
//    }},
//    { SomPaletteMod::SOURCE_DARKEST, {
//      { synthPtr, Synth::SINK_BACKGROUND_COLOR }
//    }},
//    { SomPaletteMod::SOURCE_RANDOM_LIGHT, {
//      { polarPitchRmsPointsModPtr, SoftCircleMod::SINK_COLOR },
//      { collageModPtr, CollageMod::SINK_COLOR },
//      { sandLineModPtr, SandLineMod::SINK_POINT_COLOR }
//    }}
//  });
////  connectSourceToSinks(radiiModPtr, {
////    { RandomFloatSourceMod::SOURCE_FLOAT, {
////      { clusterPointsModPtr, SoftCircleMod::SINK_POINT_RADIUS_MEAN }
////    }}
////  });
//  connectSourceToSinks(clusterModPtr, {
//    { ClusterMod::SOURCE_CLUSTER_CENTRE_VEC2, {
//      { clusterPointsModPtr, SoftCircleMod::SINK_POINTS },
//      { fluidRadialImpulseModPtr, FluidRadialImpulseMod::SINK_POINTS },
//      { dividedAreaModPtr, DividedAreaMod::SINK_MAJOR_ANCHORS },
//      { sandLineModPtr, SandLineMod::SINK_POINTS }
//    }}
//  });
//  connectSourceToSinks(snapshotModPtr, {
//    { PixelSnapshotMod::SOURCE_SNAPSHOT, {
//      { collageModPtr, CollageMod::SINK_SNAPSHOT_FBO }
//    }}
//  });
//  connectSourceToSinks(pathModPtr, {
//    { PathMod::SOURCE_PATH, {
//      { collageModPtr, CollageMod::SINK_PATH },
//      { dividedAreaModPtr, DividedAreaMod::SINK_MINOR_PATH }
//    }}
//  });
//  connectSourceToSinks(synthPtr, {
//    { Synth::SOURCE_COMPOSITE_FBO, {
//      { snapshotModPtr, PixelSnapshotMod::SINK_SNAPSHOT_SOURCE },
//      { dividedAreaModPtr, DividedAreaMod::SINK_BACKGROUND_SOURCE }
//    }}
//  });
//
//  // ***************************************************************************
//  // TODO: need params for the layer fades
//  // ***************************************************************************
//  
//  // Make some drawing layers
//  auto fluidDrawingLayerPtr = synthPtr->addDrawingLayer("1fluid",
//                                            synthPtr->getSize() / 8.0, GL_RGBA16F, GL_REPEAT,
//                                            0.0f, OF_BLENDMODE_ALPHA, true, 0);
//  auto fluidVelocitiesDrawingLayerPtr = synthPtr->addDrawingLayer("fluidVelocities",
//                                                      synthPtr->getSize() / 8.0, GL_RGB16F, GL_REPEAT,
//                                                      0.0f, OF_BLENDMODE_DISABLED, false, 0, false); // not drawn
//  auto smearedDrawingLayerPtr = synthPtr->addDrawingLayer("2smear",
//                                                synthPtr->getSize(), GL_RGBA16F, GL_REPEAT,
//                                                0.0f, OF_BLENDMODE_ADD, true, 0);
//  //  addFboConfigPtr(fboConfigPtrs, "sandlines", fboSandlinesPtr, size, GL_RGBA16F, GL_CLAMP_TO_EDGE, false, OF_BLENDMODE_ADD);
//  auto particlesDrawingLayerPtr = synthPtr->addDrawingLayer("3particles",
//                                              synthPtr->getSize()/2.0, GL_RGBA, GL_CLAMP_TO_EDGE,
//                                              1.0f, OF_BLENDMODE_ADD, false, 0);
//  auto minorLinesDrawingLayerPtr = synthPtr->addDrawingLayer("4minor lines",
//                                              synthPtr->getSize(), GL_RGBA8, GL_CLAMP_TO_EDGE,
//                                              1.0f, OF_BLENDMODE_ALPHA, false, 2);
//  auto collageDrawingLayerPtr = synthPtr->addDrawingLayer("5collage",
//                                            synthPtr->getSize(), GL_RGBA16F, GL_CLAMP_TO_EDGE,
//                                            0.0005f, OF_BLENDMODE_ADD, true, 0);
//  auto collageOutlinesDrawingLayerPtr = synthPtr->addDrawingLayer("6collage outlines",
//                                                    synthPtr->getSize(), GL_RGBA16F, GL_CLAMP_TO_EDGE,
//                                                    0.01f, OF_BLENDMODE_ALPHA, false, 0);
//  //  addFboConfigPtr(fboConfigPtrs, "cluster particles", fboClusterParticlesPtr, size, GL_RGBA16F, GL_CLAMP_TO_EDGE, false, OF_BLENDMODE_ALPHA, false, 0);
//  auto majorLinesDrawingLayerPtr = synthPtr->addDrawingLayer("7major lines",
//                                              synthPtr->getSize(), GL_RGBA16F, GL_CLAMP_TO_EDGE,
//                                              1.0f, OF_BLENDMODE_ALPHA, false, 0); // samples==4 is too much; 2 seems to push over the edge as well
//  
//  // Assign drawing surfaces to the Mods
//  assignDrawingLayerPtrToMods(particlesDrawingLayerPtr, {
//    { particleFieldModPtr },
//    { sandLineModPtr }
//  });
//  assignDrawingLayerPtrToMods(fluidDrawingLayerPtr, {
//    { fluidModPtr },
//    { clusterPointsModPtr },
////    { pitchRmsPointsModPtr },
//    { particleFieldModPtr },
////    { collageModPtr },
//  });
//  assignDrawingLayerPtrToMods(fluidVelocitiesDrawingLayerPtr, {
//    { fluidModPtr, FluidMod::VELOCITIES_LAYERPTR_NAME },
//    { fluidRadialImpulseModPtr },
//    { particleFieldModPtr },
//    { rawFluidRadialImpulseModPtr }
//  });
//  assignDrawingLayerPtrToMods(smearedDrawingLayerPtr, {
//    { clusterPointsModPtr },
//    { pitchRmsPointsModPtr },
//    { smearModPtr },
//    { sandLineModPtr },
////    { collageModPtr },
////    { collageModPtr, CollageMod::OUTLINE_LAYERPTR_NAME },
//    { particleFieldModPtr }
//  });
//  assignDrawingLayerPtrToMods(collageDrawingLayerPtr, {
//    { collageModPtr },
//    { sandLineModPtr },
//    { clusterPointsModPtr }
//  });
//  assignDrawingLayerPtrToMods(collageOutlinesDrawingLayerPtr, {
//    { collageModPtr, CollageMod::OUTLINE_LAYERPTR_NAME }
//  });
//  assignDrawingLayerPtrToMods(minorLinesDrawingLayerPtr, {
//    { dividedAreaModPtr }
//  });
//  assignDrawingLayerPtrToMods(majorLinesDrawingLayerPtr, {
//    { dividedAreaModPtr, DividedAreaMod::MAJOR_LINES_LAYERPTR_NAME }
//  });
//
//  // TODO: set up as DrawingLayers?
//  smearModPtr->receive(SmearMod::SINK_FIELD_2_FBO, fluidVelocitiesDrawingLayerPtr->fboPtr->getSource());
//  particleFieldModPtr->receive(ParticleFieldMod::SINK_FIELD_2_FBO, fluidVelocitiesDrawingLayerPtr->fboPtr->getSource());
//  
////  synthPtr->configureGui();
//  return synthPtr;
  return nullptr;
}
