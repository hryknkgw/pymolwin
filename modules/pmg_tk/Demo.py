from pymol.vfont import plain
from pymol.cgo import *
import string
import traceback

from pymol import cmd
from pymol import util

class Demo: # stateful class for doing effective demonstrations

   def __init__(self):
      self.last = None
      
   def __call__(self,name):
      if self.last:
         self.last(cleanup=1)
      if hasattr(self,name):
         self.last = getattr(self,name)
         self.last()
                 
   def rep_old(self,cleanup=0):
      if not cleanup:
         try:
            cmd.set("suspend_updates",1,quiet=1)
            cmd.disable()
            cmd.do("cd $PYMOL_PATH")
            cmd.delete("pept")
            cmd.delete("pept_dist")
            cmd.load("test/dat/pept.pdb")
            cmd.show("sticks","(pept and not i;5:7)")
            cmd.show("surface","(pept and i;5,6)")
            cmd.show("mesh","(pept and i;1,11,12,13)")
            cmd.show("spheres","(pept and i;2,12,9,4 and not n;c,n,o,ca)")
            cmd.show("dots","(i;8)")
            cmd.dist("pept_dist","(pept and i;1&n;OD2)","(pept and i;13&n;OG1)")
            cmd.set("dot_width","2");
         finally:
            cmd.set("suspend_updates",0,quiet=1)
      else:
         cmd.delete("pept")
         cmd.delete("pept_dist")

   def rep(self,cleanup=0):
      rep_list = [ "lines","sticks","spheres","surface","mesh","dots","ribbon","cartoon" ]
      try:
         if not cleanup:
            cmd.disable()
            cmd.set("suspend_updates",1,quiet=1)
            cmd.load("$PYMOL_PATH/test/dat/pept.pdb","rep1")
            cmd.alter("rep1///1-5+8-13/","ss='S'")
            cmd.cartoon("auto")
            cmd.hide("everything","rep1")
            for a in range(2,9):
               cmd.create("rep%d"%a,"rep1")
            map(lambda x,y:cmd.show(x,"rep%d"%y),
                rep_list,
                range(1,9))
            cmd.reset()
            cmd.zoom("rep1",24)
            util.cbay("rep2")
            util.cbac("rep3")
            util.cbas("rep4")
            util.cbab("rep5")
            util.cbaw("rep6")            
            util.cbay("rep8")


            cmd.set("suspend_updates",0,quiet=1)
            scale=0.5
            for b in range(1,20):
               cmd.set("suspend_updates",0,quiet=1)
               cmd.refresh()
               cmd.set("suspend_updates",1,quiet=1)
               xt=-3.2
               yt=1.6
               for a in range(1,5):
                  cmd.translate([xt*scale,yt*scale,0],object="rep%d"%a,camera=0)
                  xt=xt+2
               yt=-yt
               xt=-3.2
               for a in range(5,9):
                  cmd.translate([xt*scale,yt*scale,0],object="rep%d"%a,camera=0)
                  xt=xt+2
            for a in range(1,9):
               cmd.origin("rep%d"%a,object="rep%d"%a)
            cmd.mset("1")
            st = string.join(map(lambda x,y:"rotate angle=3,object=rep%d,axis=%s;"%(x,y),range(1,9),
                                 ['x','y','x','y','x','y','x','y']))
            cmd.mdo(1,st)
            cmd.set("suspend_updates",0,quiet=1)
            cmd.mplay()

            cgo = []
            axes = [[4.5,0.0,0.0],[0.0,3.0,0.0],[0.0,0.0,3.0]]

            c = 1
            for a in rep_list:
               ext = cmd.get_extent("rep%d"%c)
               pos = [(ext[0][0]+ext[1][0])/2,
                      (ext[0][1]+ext[1][1])/2+14,
                      (ext[0][2]+ext[1][2])/2]
               c = c + 1
               pos[0]=pos[0]-(measure_text(plain,a,axes)/2)
               wire_text(cgo,plain,pos,a,axes)
            cmd.set("cgo_line_width",1.5)
            cmd.set("auto_zoom",0)
            cmd.load_cgo(cgo,'reps')
            cmd.set("auto_zoom",1)
         else:
            cmd.delete("rep*")
            cmd.mset()
            cmd.mstop()
      except:
         traceback.print_exc()
         
   def raster3d(self,cleanup=0):
      if not cleanup:
         cmd.disable()
         cmd.load("$TUT/1hpv.r3d","cgo1")
#         cmd.delete("cgo1")
#         cmd.delete("cgo2")
#         cmd.load("test/dat/pept.r3d","cgo")
#         cmd.load("test/dat/3al1.r3d","cgo")

         cmd.zoom()
      else:
         cmd.delete("cgo1")
#         cmd.delete("cgo")

   def cgo(self,cleanup=0):
      if not cleanup:
         cmd.disable()
         cmd.do("cd $PYMOL_PATH")
         cmd.do("run examples/devel/cgo03.py")
      else:
         cmd.delete("cgo03")
         cmd.mset()
         cmd.mstop()

   def anime_old(self,cleanup=0):
      if not cleanup:
         cmd.disable()
         cmd.delete("arg")
         cmd.fragment("arg")
         cmd.zoom("arg",2)
         cmd.show("sticks","arg")
         cmd.feedback('dis','sel','res')
         for a in xrange(1,181):
            try:
               cmd.set("suspend_updates",1,quiet=1)
               cmd.edit("(arg and n;cd)","(arg and n;cg)",quiet=1)
               cmd.torsion("6")
               cmd.unpick()
               cmd.edit("(arg and n;cb)","(arg and n;ca)",quiet=1)
               cmd.torsion("2")
               cmd.unpick()
            finally:
               cmd.set("suspend_updates",0,quiet=1)         
            cmd.refresh()
         cmd.feedback('ena','sel','res')
      else:
         cmd.delete("arg")

   def anime(self,cleanup=0):
      if not cleanup:
         try:
            cmd.set("suspend_updates",1,quiet=1)
            cmd.disable()
            cmd.load("$TUT/1hpv.pdb")
            util.chainbow("1hpv")
            cmd.hide("everything","1hpv")
            cmd.show("cartoon","1hpv")
            cmd.show("sticks","1hpv///200/")
            cmd.create("1hpv_a","1hpv//A//")
            cmd.set("cartoon_smooth_loops",0,"1hpv_a")
            cmd.create("1hpv_b","1hpv//B//")
            cmd.set("cartoon_smooth_loops",0,"1hpv_b")
            cmd.create("1hpv_l","1hpv///200/")
            util.cbay("1hpv_l")
            cmd.delete("1hpv")
            cmd.set_view ((\
        0.374249548,   -0.517475128,    0.769516647,\
        -0.214397043,   -0.855623126,   -0.471108317,\
        0.902203023,    0.011330833,   -0.431161582,\
        -0.000023194,   -0.000007302, -125.089942932,\
        11.953758240,   20.323493958,    8.406080246,\
        75.304412842,  189.396347046,    0.000000000 ))
            cmd.translate([-20,0,0],object="1hpv_a")
            cmd.translate([20,0,0],object="1hpv_b")
            cmd.zoom("center",30)
            cmd.translate([0,10,00],object="1hpv_l")
         finally:
            cmd.set("suspend_updates",0,quiet=1)
         cmd.refresh()
         for a in range(1,21):
            try:
               cmd.set("suspend_updates",1,quiet=1)
               cmd.translate([1,0,0],object="1hpv_a")
               cmd.translate([-1,0,0],object="1hpv_b")
               cmd.translate([0,-0.5,0],object="1hpv_l")
            finally:
               cmd.set("suspend_updates",0,quiet=1)
            cmd.refresh()
         for a in range(1,62):
            cmd.turn("y",6)
            cmd.move('z',2)
            cmd.move('y',-0.12)
            cmd.refresh()
                        
      else:
         cmd.delete("1hpv_*")

   def roving(self,cleanup=0):
      if not cleanup:
         cmd.load("$PYMOL_PATH/test/dat/il2.pdb")
         cmd.remove("hydro")
         cmd.disable()
         cmd.enable("il2")
         cmd.set("ribbon_color","blue","il2")
         cmd.set("roving_detail",1)
         cmd.set("roving_origin",1)
         cmd.set("stick_radius",0.12,"il2")
#         cmd.zoom("/il2///16/O")
#         cmd.zoom("center",12)
         cmd.set_view ((\
     0.132852688,   -0.729740858,    0.670686543,\
     -0.228543565,    0.635894477,    0.737154961,\
     -0.964425683,   -0.251212329,   -0.082298420,\
     0.000062190,    0.000183226,  -58.861488342,\
     13.349151611,   -1.565427899,   22.383148193,\
     55.259441376,   63.259449005,    0.000000000 ))
         cmd.wizard("message","Middle-Click to rove...         CTRL-SHIFT-Middle-Click to center...")
      else:
         cmd.delete("il2")
         cmd.set_wizard()         
         cmd.set("roving_detail",0)
         cmd.refresh()
         cmd.delete("rov_*")
         
   def roving_density(self,cleanup=0):
      if not cleanup:
         try:
            cmd.load("$PYMOL_PATH/test/dat/il2.pdb")
            cmd.set("suspend_updates",1,quiet=1)
            cmd.remove("hydro")
            cmd.disable()
            cmd.enable("il2")
            cmd.map_new("map","gaussian","0.75","il2")
            cmd.set("ribbon_color","purple","il2")
            cmd.set("roving_detail",1)
            cmd.set("roving_origin",1)
            cmd.set("stick_radius",0.12,"il2")
            cmd.set("roving_sticks",0)
            cmd.set("roving_polar_contacts",0)
            cmd.set("line_width","3")
            cmd.set("roving_map1_name","map")
            cmd.isomesh("rov_m1","map",9999.0,"il2")
            cmd.color("density","rov_m1")
            
            cmd.set_view ((\
        0.132852688,   -0.729740858,    0.670686543,\
        -0.228543565,    0.635894477,    0.737154961,\
        -0.964425683,   -0.251212329,   -0.082298420,\
        0.000062190,    0.000183226,  -58.861488342,\
        13.349151611,   -1.565427899,   22.383148193,\
        55.259441376,   63.259449005,    0.000000000 ))
            cmd.wizard("message","Middle-Click to rove...         CTRL-SHIFT-Middle-Click to center...")

         finally:
            cmd.set("suspend_updates",0,quiet=1)
         cmd.refresh()
      else:
         cmd.set("roving_map1_name","")
         cmd.set("roving_polar_contacts",7)
         cmd.set("roving_sticks",6)
         cmd.delete("il2")
         cmd.delete("map")
         cmd.set("line_width",1.5)
         cmd.set_wizard()
         cmd.set("roving_detail",0)
         cmd.refresh()
         cmd.delete("rov_*")
         
   def cartoon(self,cleanup=0):
      if not cleanup:
         try:
            cmd.set("suspend_updates",1,quiet=1)
            cmd.disable()
            cmd.delete("1tii")      
            cmd.load("$PYMOL_PATH/test/dat/1tii.pdb")
            cmd.hide("(1tii)")
            cmd.show("cartoon","1tii")
            cmd.zoom("1tii")
            cmd.spectrum("count","rainbow","1tii")
            cmd.set("cartoon_highlight_color","grey50","1tii")
            cmd.set("cartoon_fancy_helices",1,"1tii")
         finally:
            cmd.set("suspend_updates",0,quiet=1)
         cmd.refresh()
      else:
         cmd.delete("1tii")

   def elec(self,cleanup=0):
      if not cleanup:
         cmd.disable()
         cmd.delete("pept")
         cmd.delete("e_pot")
         cmd.delete("e_lvl")
         cmd.load("$PYMOL_PATH/test/dat/pept.pkl")
         cmd.hide("(pept)")
         cmd.show("surface","pept")
         cmd.set("coulomb_dielectric",80.0)
         cmd.map_new("e_pot","coulomb",1.0,"pept",5)
         cmd.ramp_new("e_lvl","e_pot",[-3.6,-1.6,0.4])
         cmd.set("surface_color","e_lvl","pept")
         cmd.wizard("message","CTRL-Middle-Click on color bar to change levels...")
         cmd.refresh()
      else:
         cmd.delete("pept")
         cmd.delete("e_pot")
         cmd.delete("e_lvl")
         cmd.set_wizard()
         
   def trans(self,cleanup=0):
      if not cleanup:
         try:
            cmd.set("suspend_updates",1,quiet=1)
            cmd.disable()
            cmd.delete("trans")
            cmd.load("$PYMOL_PATH/test/dat/pept.pdb","trans")
            cmd.hide("(trans)")
            cmd.show("surface","trans")
            cmd.show("sticks","trans")
            cmd.set("surface_color","white","trans")
            cmd.set("transparency",0.5,"trans")
            cmd.zoom("trans")
         finally:
            cmd.set("suspend_updates",0,quiet=1)
         cmd.refresh()
      else:
         cmd.delete("trans")

   def ray(self,cleanup=0):
      if not cleanup:
         cmd.set("suspend_updates",1,quiet=1)
         cmd.disable()
         cmd.delete("ray")
         cmd.load("$PYMOL_PATH/test/dat/il2.pdb","ray")
         cmd.remove("(ray and hydro)")
         cmd.hide("lines","ray")
         cmd.show("spheres","ray")
         cmd.orient("ray")
         cmd.turn("x",90)
         util.ray_shadows('heavy')
         cmd.set("suspend_updates",0,quiet=1)
         cmd.refresh()
         cmd.set("antialias",1)
         cmd.do("ray")
         cmd.set("antialias",0)
      else:
         cmd.delete("ray")

   def sculpt(self,cleanup=0):
      if not cleanup:
         cmd.set("suspend_updates",1,quiet=1)
         cmd.disable()
         cmd.delete("sculpt")
         cmd.load("$PYMOL_PATH/test/dat/pept.pdb","sculpt")
         cmd.hide("lines","sculpt")
         cmd.show("sticks","sculpt")
         cmd.show("spheres","sculpt")
         cmd.set("sphere_transparency","0.75","sculpt")
         cmd.set("sphere_color","grey","sculpt")
         cmd.frame(1)
         cmd.set("auto_sculpt",1)
         cmd.set("sculpting",1)
         cmd.sculpt_activate("sculpt")
         cmd.do("edit_mode")
         cmd.set("valence","0.05")
         cmd.set("suspend_updates",0,quiet=0)
         cmd.wizard("message","CTRL-Left-Click to drag atoms...")
      else:
         cmd.set("sculpting",0)
         cmd.set("auto_sculpt",0)
         cmd.delete("sculpt")
         cmd.set_wizard()
