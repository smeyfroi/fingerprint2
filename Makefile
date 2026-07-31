# Attempt to load a config.make file.
# If none is found, project defaults in config.project.make will be used.
ifneq ($(wildcard config.make),)
	include config.make
endif

# make sure the OF_ROOT location is defined
ifndef OF_ROOT
	OF_ROOT=../../..
endif

# ─── Syphon.framework into the make-built bundle ─────────────────────────────
# The Xcode build embeds Syphon via a copy-files phase; OF's make build
# re-creates bin/<app>.app from scratch on EVERY build (afterplatform does
# `rm -rf`) and ships it WITHOUT the framework — instant dyld crash at launch
# ("Library not loaded: @loader_path/../Frameworks/Syphon.framework"). OF's
# makefiles have no post-bundle hook, so the Release/Debug goals are wrapped:
# the outer make runs the real OF build in a sub-make, then bundles Syphon.
# Everything else (clean, run, help, …) forwards to the inner make verbatim.
SYPHON_FRAMEWORK = $(OF_ROOT)/addons/ofxSyphon/libs/Syphon/lib/osx/Syphon.framework

.PHONY: bundle_syphon
bundle_syphon:
	@for app in bin/*.app; do \
	  if [ -d "$$app/Contents/MacOS" ] && [ ! -d "$$app/Contents/Frameworks/Syphon.framework" ]; then \
	    mkdir -p "$$app/Contents/Frameworks"; \
	    ditto "$(SYPHON_FRAMEWORK)" "$$app/Contents/Frameworks/Syphon.framework"; \
	    echo "bundled Syphon.framework into $$app"; \
	  fi; \
	done

ifndef FP2_INNER

.PHONY: Release Debug
Release Debug:
	@$(MAKE) $@ FP2_INNER=1
	@$(MAKE) bundle_syphon

# Forward every other goal (clean, RunRelease, help, …) to the real makefiles.
.DEFAULT:
	@$(MAKE) $@ FP2_INNER=1

else

# call the project makefile!
include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk

endif
