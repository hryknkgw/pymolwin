
include Rules.make

all: unix contrib

PRIME = ls *.c | sed 's/.c$$/.o/'| awk 'BEGIN{printf("OBJS=")}{printf("%s ",$$1)}END{print}'>.files;ls *.c | sed 's/.c$$/.p/'| awk 'BEGIN{printf("DEPS=")}{printf("%s ",$$1)}END{print}'>>.files; touch .depends; cat .files .depends > .includes

MINDEP=$(PYMOL_PATH)/products/unix-mindep
MDP=$(MINDEP)/pymol

.includes:
	cd layer0;$(PRIME)
	cd layer1;$(PRIME)
	cd layer2;$(PRIME)
	cd layer3;$(PRIME)
	cd layer4;$(PRIME)
	cd layer5;$(PRIME)
	touch .includes

.update:
	cd layer0;$(MAKE)
	cd layer1;$(MAKE)
	cd layer2;$(MAKE)
	cd layer3;$(MAKE)
	cd layer4;$(MAKE)
	cd layer5;$(MAKE)
	touch .update

0:
	cd layer0;$(MAKE)

1:
	cd layer1;$(MAKE)

2:
	cd layer2;$(MAKE)

3:
	cd layer3;$(MAKE)

4:
	cd layer4;$(MAKE)

5:
	cd layer5;$(MAKE)

.depends: 
	/bin/rm -f .includes
	cd layer0;$(MAKE) depends
	cd layer1;$(MAKE) depends
	cd layer2;$(MAKE) depends
	cd layer3;$(MAKE) depends
	cd layer4;$(MAKE) depends
	cd layer5;$(MAKE) depends

.contrib:
	cd contrib;$(MAKE)
	touch .contrib

contrib: .contrib

unix: .includes .depends .update 
	/bin/rm -f .update .includes
	cc $(BUILD) $(DEST) */*.o $(CFLAGS)  $(LIB_DIRS) $(LIBS)	

semistatic: .includes .depends .update
	/bin/rm -f .update .includes
	cd contrib;$(MAKE) static
	cc $(BUILD) $(DEST) */*.o $(CFLAGS)  $(LIB_DIRS) $(LIBS)	

unix-mindep-build: semistatic
	$(PYTHON_EXE) modules/compile_pymol.py
	/bin/rm -rf $(MINDEP)
	install -d $(MDP)/ext/lib
	cp -r modules $(MDP)
	cp -r test $(MDP)
	cp -r data $(MDP)	
	cp -r examples $(MDP)
	cp -r scripts $(MDP)
	cp -r pymol.exe $(MDP)
	cp -r ext/lib/python2.2 $(MDP)/ext/lib
	cp -r ext/lib/tcl8.4 $(MDP)/ext/lib
	cp -r ext/lib/tk8.4 $(MDP)/ext/lib
	/bin/rm -f $(MDP)/ext/lib/python2.2/config/libpython2.2.a
	/bin/rm -rf $(MDP)/ext/lib/python2.2/test
	cp LICENSE $(MDP)
	cp README $(MDP)
	cp setup/INSTALL.unix-mindep $(MDP)/INSTALL
	cp setup/setup.sh.unix-mindep $(MDP)/setup.sh
	cd $(MINDEP);chown -R nobody pymol
	cd $(MINDEP);chgrp -R nobody pymol

unix-mindep: unix-mindep-build
	cd $(MINDEP);tar -cvf - pymol | gzip > ../pymol-0_xx-bin-xxxxx-mindep.tgz

unix-fedora: unix-mindep-build
	cp setup/setup.sh.unix-fedora $(MDP)/setup.sh
	cd $(MINDEP);tar -cvf - pymol | gzip > ../fedorapymol-0_xx-bin-xxxxx-mindep.tgz

irix-mindep: semistatic
	$(PYTHON_EXE) modules/compile_pymol.py
	/bin/rm -rf $(MINDEP)
	mkdir products/unix-mindep
	mkdir products/unix-mindep/pymol
	mkdir products/unix-mindep/pymol/ext
	mkdir products/unix-mindep/pymol/ext/lib
	cp -r modules $(MDP)
	cp -r test $(MDP)
	cp -r data $(MDP)	
	cp -r examples $(MDP)
	cp -r pymol.exe $(MDP)
	cp -r ext/lib/python2.2 $(MDP)/ext/lib
	cp -r ext/lib/tcl8.4 $(MDP)/ext/lib
	cp -r ext/lib/tk8.4 $(MDP)/ext/lib
	/bin/rm -f $(MDP)/ext/lib/python2.2/config/libpython2.2.a
	/bin/rm -rf $(MDP)/ext/lib/python2.2/test
	cp LICENSE $(MDP)
	cp README $(MDP)
	cp setup/INSTALL.unix-mindep $(MDP)/INSTALL
	cp setup/setup.sh.unix-mindep $(MDP)/setup.sh
	cd $(MINDEP);chown -R nobody.nobody pymol
	cd $(MINDEP);tar -cvf - pymol |gzip > ../pymol-0_xx-bin-xxxxx-mindep.tgz

windows: .includes .depends .update 
	echo "EXPORTS" > _cmd.def
	nm --demangle --defined-only */*.o | grep ' T ' | sed 's/.* T //' >> _cmd.def 
	dllwrap --dllname _cmd.pyd --driver-name gcc $(BUILD) --def _cmd.def -s  */*.o $(CFLAGS) $(LIB_DIRS) $(LIBS)
	/bin/rm -f .update .includes

fast: .update
	/bin/rm -f .update 
	cc $(BUILD) */*.o $(CFLAGS) $(LIB_DIRS) $(LIBS)

depends: 
	/bin/rm -f */*.p
	$(MAKE) .depends

partial:
	touch layer5/main.c
	touch layer1/P.c
	touch layer4/Cmd.c
	/bin/rm -f modules/pymol/_cmd.so pymol.exe
	$(MAKE)

clean: 
	touch .no_fail
	/bin/rm -f layer*/*.o layer*/*.p modules/*/*.pyc modules/*/*/*.pyc \
	layer*/.files layer*/.depends layer*/.includes \
	*.log core */core game.* log.* _cmd.def .update .contrib .no_fail*
	cd contrib;$(MAKE) clean

distclean: clean
	touch .no_fail
	/bin/rm -f modules/*.pyc modules/*.so modules/*/*.so modules/*/*.so \
	modules/*/*/*/*.so pymol.exe \
	modules/*/*.pyc modules/*/*/*.pyc modules/*/*/*/*.pyc .no_fail* test/cmp/*
	/bin/rm -rf build
	/bin/rm -rf products/*.tgz products/unix-mindep
	cd contrib;$(MAKE) distclean

pyclean: clean
	/bin/rm -rf build
	/bin/rm -rf ext/lib/python2.1/site-packages/pymol
	/bin/rm -rf ext/lib/python2.1/site-packages/chempy
	/bin/rm -rf ext/lib/python2.1/site-packages/pmg_tk
	/bin/rm -rf ext/lib/python2.1/site-packages/pmg_wx

dist: distclean
	cd ..;tar -cvf - pymol | gzip > pymol.tgz

pmw: 
	cd modules; gunzip < ./pmg_tk/pmw.tgz | tar xvf -

compileall:
	$(PYTHON_EXE) modules/compile_pymol.py

# Everything below here is for the MacPyMOL Incentive Product, which is
# not currently Open-Source (though that may change in the future).
# Compilation of MacPyMOL requires layerOSX.

OSXPROD=products/MacPyMOL.app
OSXFREE=products/PyMOL.app
OSXFEDORA=products/FedoraPyMOL.app
OSXFRWK=products/FrameworkPyMOL.app
OSXDEMO=products/PyMOL\ Demos
OSXPYMOL=$(OSXPROD)/pymol
OSXEXE=$(OSXPROD)/Contents/MacOS/PyMOL
OSXPY=$(OSXPROD)/py23

osx-wrap:
	/bin/rm -rf $(OSXPYMOL) $(OSXEXE) $(OSXPY)
	/usr/local/bin/tar -czvf layerOSX/bundle/app.hfstar $(OSXPROD)

osx-unwrap:
	/bin/rm -rf $(OSXPROD)
	/usr/local/bin/tar -xzvf layerOSX/bundle/app.hfstar

osx-python-framework:
	cc layerOSX/bundle/python.c -o $(OSXEXE) \
$(PYTHON_INC_DIR) \
-framework CoreFoundation -framework Python -lc -Wno-long-double

osx-python-standalone:
	cc layerOSX/bundle/python.c -o $(OSXEXE) \
$(PYTHON_INC_DIR) -Lext/lib -lpython2.3 \
-framework CoreFoundation -lc -Wno-long-double -D_PYMOL_OSX_PYTHONHOME

osx: 
	cd layerOSX; $(MAKE)
	$(MAKE) 

osx-dev: osx
	cp modules/pymol/_cmd.so $(OSXPYMOL)/modules/pymol

osx-pdev:
	/bin/rm -rf $(OSXPYMOL)/modules/pymol
	cp -R modules/pymol $(OSXPYMOL)/modules/pymol

osx-product: osx 
	$(PYTHON_EXE) modules/compile_pymol.py
	/bin/rm -rf $(OSXPYMOL)
	install -d $(OSXPYMOL)
	cp -R modules $(OSXPYMOL)/
	cp -R test $(OSXPYMOL)/
	cp -R data $(OSXPYMOL)/	
	cp -R scripts $(OSXPYMOL)/	
	cp -R examples $(OSXPYMOL)/
	cp LICENSE $(OSXPYMOL)/
	cp README $(OSXPYMOL)/


osx-standalone: osx-unwrap osx-python-standalone osx-product
	/bin/rm -rf $(OSXPY)
	install -d $(OSXPY)/lib
	cp -R ext/lib/python2.3 $(OSXPY)/lib/

osx-wrap-demos:
	/usr/local/bin/tar -czvf layerOSX/applescript.hfstar $(OSXDEMO)

osx-unwrap-demos:
	/usr/local/bin/tar -xzvf layerOSX/applescript.hfstar

osx-demo-data:
	install -d $(OSXPYMOL)/data/demo
	cp -R demo_data/* $(OSXPYMOL)/data/demo/

osx-demo: osx-standalone osx-demo-data osx-unwrap-demos 

mac-framework: osx-unwrap osx-python-framework osx-product
	/bin/rm -rf $(OSXFRWK)
	/bin/cp -R $(OSXPROD) $(OSXFRWK)
	sed 's/MacPyMOL/FrameworkPyMOL/' $(OSXFRWK)/Contents/Info.plist > $(OSXFRWK)/Contents/Info.plist.tmp
	mv $(OSXFRWK)/Contents/Info.plist.tmp $(OSXFRWK)/Contents/Info.plist
	/bin/rm -r $(OSXFRWK)/Contents/Resources/English.lproj/MainMenu.nib
	/bin/rm -r $(OSXFRWK)/Contents/Resources/English.lproj/MainMenu~.nib

mac: osx-standalone
	/bin/cp layerOSX/bundle/splash.png $(OSXPYMOL)/data/pymol/

mac-fedora: mac
	/bin/rm -rf $(OSXFEDORA)
	/bin/cp -R $(OSXPROD) $(OSXFEDORA)
	sed 's/MacPyMOL/FedoraPyMOL/' $(OSXFEDORA)/Contents/Info.plist > $(OSXFEDORA)/Contents/Info.plist.tmp
	mv $(OSXFEDORA)/Contents/Info.plist.tmp $(OSXFEDORA)/Contents/Info.plist
	/bin/cp data/pymol/splash.png $(OSXFEDORA)/pymol/data/pymol/
	/bin/rm -r $(OSXFEDORA)/Contents/Resources/English.lproj/MainMenu.nib
	/bin/rm -r $(OSXFEDORA)/Contents/Resources/English.lproj/MainMenu~.nib

mac-free: mac
	/bin/rm -rf $(OSXFREE)
	/bin/cp -R $(OSXPROD) $(OSXFREE)
	sed 's/MacPyMOL/PyMOL/' $(OSXFREE)/Contents/Info.plist > $(OSXFREE)/Contents/Info.plist.tmp
	mv $(OSXFREE)/Contents/Info.plist.tmp $(OSXFREE)/Contents/Info.plist
	/bin/cp data/pymol/splash.png $(OSXFREE)/pymol/data/pymol/
	/bin/rm -r $(OSXFREE)/Contents/Resources/English.lproj/MainMenu.nib
	/bin/rm -r $(OSXFREE)/Contents/Resources/English.lproj/MainMenu~.nib

mac-beta:
	make distclean
	make mac
