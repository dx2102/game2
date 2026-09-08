.PHONY : all
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	BLENDER = /Applications/Blender.app/Contents/MacOS/Blender -y
else ifeq ($(UNAME_S),Linux)
	BLENDER = ../../blender-5.2.1-linux-x64/blender -y
else
	BLENDER = blender
endif
DIST=../dist
all : $(DIST)/cube.pnct $(DIST)/robot1.pnct $(DIST)/spring.pnct $(DIST)/extras.pnct
$(DIST)/%.pnct : %.blend export-meshes.py
	$(BLENDER) --background --python export-meshes.py -- '$<':Main '$@'
