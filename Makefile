include Rules.make

all: unix contrib

PRIME = ls *.c | sed 's/.c$$/.o/'| awk 'BEGIN{printf("OBJS=")}{printf("%s ",$$1)}END{print}'>.files;ls *.c | sed 's/.c$$/.p/'| awk 'BEGIN{printf("DEPS=")}{printf("%s ",$$1)}END{print}'>>.files; touch .depends; cat .files .depends > .includes

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
	cc $(BUILD) */*.o $(CFLAGS)  $(LIB_DIRS) $(LIBS)

windows: .includes .depends .update 
	echo "EXPORTS" > _pm.def
	nm --demangle --defined-only */*.o | grep ' T ' | sed 's/.* T //' >> _pm.def 
	dllwrap --dllname _pm.pyd --driver-name gcc $(BUILD) --def _pm.def -s  */*.o $(CFLAGS) $(LIB_DIRS) $(LIBS)
	/bin/rm -f .update .includes

fast: .update
	/bin/rm -f .update 
	cc $(BUILD) */*.o $(CFLAGS) $(LIB_DIRS) $(LIBS)

depends: 
	/bin/rm -f */*.p
	$(MAKE) .depends

clean: 
	touch .no_fail
	/bin/rm -f layer*/*.o layer*/*.p \
	layer*/.files layer*/.depends layer*/.includes \
	*.log core */core game.* log.* _pm.def .update .contrib .no_fail*
#	cd contrib;$(MAKE) clean

distclean: clean
	touch .no_fail
	/bin/rm -f modules/*.pyc modules/*.so pymol.exe \
	modules/Pmw/*.pyc modules/Pmw/*/*.pyc modules/Pmw/*/*/*.pyc .no_fail*
	cd contrib;$(MAKE) distclean

dist: distclean
	cd ..;tar -cvf - pymol | gzip > pymol.tgz


