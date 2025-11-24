//
//  ofAppSynth2.cpp
//  fingerprint1
//
//  Created by Steve Meyfroidt on 06/06/2025.
//

#include "ofApp.h"

using namespace ofxMarkSynth;

std::shared_ptr<Synth> createSynth2(glm::vec2 size) {
//  auto synthPtr = std::make_shared<Synth>("Synth2", ModConfig {}, START_PAUSED, size);
//  
//  // Audio source
//  auto audioDataSourceModPtr = synthPtr->addMod<AudioDataSourceMod>("Audio Source", {
//    {"MinPitch", "50.0"},
//    {"MaxPitch", "1500.0"},
//    {"MinRms", "0.0005"},
//    {"MaxRms", MAX_RMS},
//    {"MinComplexSpectralDifference", "200.0"},
//    {"MaxComplexSpectralDifference", "1000.0"},
//    {"MinSpectralCrest", "20.0"},
//    {"MaxSpectralCrest", "350.0"},
//    {"MinZeroCrossingRate", "5.0"},
//    {"MaxZeroCrossingRate", "15.0"}
//  }, MIC_DEVICE_NAME, RECORD_AUDIO, RECORDING_PATH, ROOT_SOURCE_MATERIAL_PATH);
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
//  // Smeared raw data points
//  auto drawPointsModPtr = synthPtr->addMod<SoftCircleMod>("Raw Points", {
//    {"Radius", "0.005"}, // min
//    {"RadiusVarianceScale", "0.01"}, // scale the variance to some to the min
//    {"Softness", "1.0"},
//  });
//  
//  auto smearModPtr = synthPtr->addMod<SmearMod>("Smear Raw Points", {
//    {"Translation", "0.0, 0.0005"},
//    {"MixNew", "0.8"},
//    {"AlphaMultiplier", "0.999"},
//  });
//  
//  // Collage layer from raw pitch/RMS and the raw points FBO
//  auto pixelSnapshotModPtr = synthPtr->addMod<PixelSnapshotMod>("Snapshot", {});
//  
//  auto pathModPtr = synthPtr->addMod<PathMod>("Collage Path", {
//    {"MaxVertices", "7"},
//    {"MinVertexProximity", "0.01"},
//    {"MaxVertexProximity", "0.07"},
//    {"Strategy", "1"} // 0=polypath; 1=bounds; 2=horizontals; 3=convex hull
//  });
//  
//  auto collageModPtr = synthPtr->addMod<CollageMod>("Collage", {
//    {"Saturation", "1.5"},
//    {"Strategy", "1"}, // 0=tint; 1=add tinted pixels
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
//  auto fluidPointRadiusModPtr = synthPtr->addMod<RandomFloatSourceMod>("Fluid Cluster Radius", {
//    {"CreatedPerUpdate", "0.05"},
//    {"Min", "0.005"},
//    {"Max", "0.02"}
//  }, std::pair<float, float>{0.0, 0.1}, std::pair<float, float>{0.0, 0.1});
//  
//  auto drawClusterPointsModPtr = synthPtr->addMod<SoftCircleMod>("Fluid Clusters", {
//    {"ColorMultiplier", "0.5"},
//    {"AlphaMultiplier", "0.3"},
//    {"Softness", "0.3"}
//  });
//  
//  // Radial fluid impulses from clusters
//  auto clusterRadialImpulseModPtr = synthPtr->addMod<FluidRadialImpulseMod>("Cluster Impulses", {
//    {"ImpulseRadius", "0.05"},
//    {"ImpulseStrength", "0.02"}
//  });
//  
//  // Raw data points into fluid
//  auto drawRawPointsModPtr = synthPtr->addMod<SoftCircleMod>("Fluid Raw Points", {
//    {"Radius", "0.01"},
//    {"ColorMultiplier", "0.4"},
//    {"AlphaMultiplier", "0.85"},
//    {"Softness", "0.1"}
//  });
//  
//  // Radial fluid impulses from raw points
//  auto rawRadialImpulseModPtr = synthPtr->addMod<FluidRadialImpulseMod>("Raw Point Impulses", {
//    {"ImpulseRadius", "0.02"},
//    {"ImpulseStrength", "0.06"}
//  });
//  
//  auto videoFlowModPtr = synthPtr->addMod<VideoFlowSourceMod>("Video flow", {
//    { "offset", "2.0" },
//    { "threshold", "0.4" },
//    { "force", "5.0" },
//    { "power", "0.8" }
//  }, VIDEO_DEVICE_ID, glm::vec2 { 1280, 720 }, RECORD_VIDEO, saveFilePath("video-recordings"));
//  //  auto videoFlowModPtr = addMod<VideoFlowSourceMod>(mods, "Video flow", {}, rootSourceMaterialPath/"trombone-trimmed.mov", true);
//  
//  // Video ParticleField
//  auto particleFieldModPtr = synthPtr->addMod<ParticleFieldMod>("Particle Field", {
//    {"velocityDamping", "0.995"},
//    {"forceMultiplier", "0.5"},
//    {"maxVelocity", "0.0005"},
//    {"particleSize", "3.0"}
//  }, 0.0, 0.0);
//  
//  //  { // Motion particles
//  //    auto particleSetModPtr = addMod<ParticleSetMod>(mods, "Motion Particles", {
//  //      { "strategy", "0" }, // POINTS, CONNECTIONS, BOTH
//  //      { "maxParticles", "1000" },
//  //      { "maxParticleAge", "100" },
//  //      { "particleVelocityDamping", "0.995" },
//  //      { "particleAttraction", "-0.01" },
//  //      { "particleAttractionRadius", "0.2" },
//  //      { "particleConnectionRadius", "0.04" },
//  //      { "particleDrawRadius", "8.0" },
//  //      { "colourMultiplier", "1.0" },
//  //      { "forceScale", "0.05" },
//  //      { "Spin", "0.001" }
//  //    });
//  //    videoFlowModPtr->addSink(VideoFlowSourceMod::SOURCE_VEC4, particleSetModPtr, ParticleSetMod::SINK_POINT_VELOCITIES);
//  //    audioPaletteModPtr->addSink(SomPaletteMod::SOURCE_RANDOM_LIGHT_VEC4, particleSetModPtr, DrawPointsMod::SINK_POINT_COLOR);
//  //
//  //    auto multiplyModPtr = addMod<MultiplyMod>(mods, "Fade Particles", {
//  //      {"Multiply By", "0.99"}
//  //    });
//  //    particleSetModPtr->addSink(ParticleSetMod::SOURCE_FBO, multiplyModPtr, MultiplyMod::SINK_FBO);
//  //
//  //    particleSetModPtr->receive(ParticleSetMod::SINK_FBO, fboMotionParticlesPtr);
//  //  }
//  
//  connectSourceToSinks(audioDataSourceModPtr, {
//    { AudioDataSourceMod::SOURCE_SPECTRAL_3D_POINTS, {
//      { audioPaletteModPtr, SomPaletteMod::SINK_VEC3 }}
//    },
////    { AudioDataSourceMod::SOURCE_SPECTRAL_CREST_SCALAR, {
////      { drawPointsModPtr, SoftCircleMod::SINK_POINT_RADIUS_VARIANCE }}
////    },
//    { AudioDataSourceMod::SOURCE_PITCH_SCALAR, {
//      { drawPointsModPtr, SoftCircleMod::SINK_COLOR_MULTIPLIER }}
//    },
//    { AudioDataSourceMod::SOURCE_POLAR_PITCH_RMS_POINTS, {
//      { clusterModPtr, ClusterMod::SINK_VEC2 },
//      { drawPointsModPtr, SoftCircleMod::SINK_POINTS },
//      { pathModPtr, PathMod::SINK_VEC2 },
//      { dividedAreaModPtr, DividedAreaMod::SINK_MINOR_ANCHORS },
//      { drawRawPointsModPtr, SoftCircleMod::SINK_POINTS },
//      { rawRadialImpulseModPtr, FluidRadialImpulseMod::SINK_POINTS }}
//    },
//    { AudioDataSourceMod::SOURCE_ONSET1, {
//      //      { synthPtr, Synth::SINK_AUDIO_ONSET }
//    }},
//    { AudioDataSourceMod::SOURCE_TIMBRE_CHANGE, {
//      //      { synthPtr, Synth::SINK_AUDIO_TIMBRE_CHANGE }
//    }}
//  });
//  connectSourceToSinks(audioPaletteModPtr, {
//    { SomPaletteMod::SOURCE_RANDOM_LIGHT, {
//      { drawPointsModPtr, SoftCircleMod::SINK_COLOR },
//      { collageModPtr, CollageMod::SINK_COLOR },
//      { sandLineModPtr, SandLineMod::SINK_POINT_COLOR }}
//    },
//    { SomPaletteMod::SOURCE_DARKEST, {
//      { synthPtr, Synth::SINK_BACKGROUND_COLOR }}
//    },
//    { SomPaletteMod::SOURCE_RANDOM, {
//      { drawClusterPointsModPtr, SoftCircleMod::SINK_COLOR }}
//    },
//    { SomPaletteMod::SOURCE_RANDOM_DARK, {
//      { drawRawPointsModPtr, SoftCircleMod::SINK_COLOR }}
//    },
//    { SomPaletteMod::SOURCE_FIELD, {
//      { particleFieldModPtr, ParticleFieldMod::SINK_FIELD_2_FBO }}
//    }
//   });
//  connectSourceToSinks(clusterModPtr, {
//    { ClusterMod::SOURCE_CLUSTER_CENTRE_VEC2, {
//      { dividedAreaModPtr, DividedAreaMod::SINK_MAJOR_ANCHORS },
//      { sandLineModPtr, SandLineMod::SINK_POINTS },
//      { drawClusterPointsModPtr, SoftCircleMod::SINK_POINTS },
//      { clusterRadialImpulseModPtr, FluidRadialImpulseMod::SINK_POINTS }}
//    }
//  });
//  connectSourceToSinks(pixelSnapshotModPtr, {
//    { PixelSnapshotMod::SOURCE_SNAPSHOT, {
//      { collageModPtr, CollageMod::SINK_SNAPSHOT_FBO }}
//    }
//  });
//  connectSourceToSinks(pathModPtr, {
//    { PathMod::SOURCE_PATH, {
//      { collageModPtr, CollageMod::SINK_PATH },
//      { dividedAreaModPtr, DividedAreaMod::SINK_MINOR_PATH }}
//    }
//  });
//  connectSourceToSinks(fluidPointRadiusModPtr, {
//    { RandomFloatSourceMod::SOURCE_FLOAT, {
//      { drawClusterPointsModPtr, SoftCircleMod::SINK_RADIUS }}
//    }
//  });
//  connectSourceToSinks(videoFlowModPtr, {
//    { VideoFlowSourceMod::SOURCE_FLOW_FBO, {
//      { particleFieldModPtr, ParticleFieldMod::SINK_FIELD_1_FBO }}
//    }
//  });
//  connectSourceToSinks(synthPtr, {
//    { Synth::SOURCE_COMPOSITE_FBO, {
//      { pixelSnapshotModPtr, PixelSnapshotMod::SINK_SNAPSHOT_SOURCE },
//      { dividedAreaModPtr, DividedAreaMod::SINK_BACKGROUND_SOURCE }}
//    }
//  });
//
//  auto fluidDrawingLayerPtr = synthPtr->addDrawingLayer("1fluid",
//                                          synthPtr->getSize() / 8.0, GL_RGBA16F, GL_REPEAT,
//                                          0.0f, OF_BLENDMODE_ALPHA, false, 0);
//  auto fluidVelocitiesDrawingLayerPtr = synthPtr->addDrawingLayer("fluidVelocities",
//                                                    synthPtr->getSize() / 8.0, GL_RGB16F, GL_REPEAT,
//                                                    0.0f, OF_BLENDMODE_DISABLED, false, 0, false); // not drawn
//  auto smearedDrawingLayerPtr = synthPtr->addDrawingLayer("2smear",
//                                              synthPtr->getSize(), GL_RGBA16F, GL_REPEAT,
//                                              0.0f, OF_BLENDMODE_ADD, false, 0);
//  auto motionParticlesDrawingLayerPtr = synthPtr->addDrawingLayer("3motion particles",
//                                                    synthPtr->getSize()/2.0, GL_RGBA, GL_CLAMP_TO_EDGE,
//                                                    0.0f, OF_BLENDMODE_ADD, false, 0);
//  auto minorLinesDrawingLayerPtr = synthPtr->addDrawingLayer("4minor lines",
//                                                synthPtr->getSize(), GL_RGBA8, GL_CLAMP_TO_EDGE,
//                                                1.0f, OF_BLENDMODE_ALPHA, false, 4);
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
//  auto sandlinesDrawingLayerPtr = synthPtr->addDrawingLayer("8sandlines",
//                                              synthPtr->getSize(), GL_RGBA16F, GL_CLAMP_TO_EDGE,
//                                              false, OF_BLENDMODE_ADD, false, 0);
//  
//  // Assign drawing surfaces to the Mods
//  assignDrawingLayerPtrToMods(fluidDrawingLayerPtr, {
//    { fluidModPtr },
//    { drawClusterPointsModPtr },
//    { drawRawPointsModPtr }
//  });
//  assignDrawingLayerPtrToMods(fluidVelocitiesDrawingLayerPtr, {
//    { fluidModPtr, FluidMod::VELOCITIES_LAYERPTR_NAME },
//    { clusterRadialImpulseModPtr },
//    { rawRadialImpulseModPtr }
//  });
//  assignDrawingLayerPtrToMods(smearedDrawingLayerPtr, {
//    { drawPointsModPtr },
//    { smearModPtr },
//    { sandLineModPtr }
//  });
//  assignDrawingLayerPtrToMods(collageDrawingLayerPtr, {
//    { collageModPtr }
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
//  assignDrawingLayerPtrToMods(motionParticlesDrawingLayerPtr, {
//    { particleFieldModPtr }
//  });
//
//  // TODO: these aren't right are they?
//  smearModPtr->receive(SmearMod::SINK_FIELD_2_FBO, fluidVelocitiesDrawingLayerPtr->fboPtr->getSource());
//  particleFieldModPtr->receive(ParticleFieldMod::SINK_FIELD_2_FBO, fluidVelocitiesDrawingLayerPtr->fboPtr->getSource());
//
////  synthPtr->configureGui();
//  return synthPtr;
  return nullptr;
}
