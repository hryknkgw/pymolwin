#A* -------------------------------------------------------------------
#B* This file contains source code for the PyMOL computer program
#C* copyright 1998-2000 by Warren Lyford Delano of DeLano Scientific. 
#D* -------------------------------------------------------------------
#E* It is unlawful to modify or remove this copyright notice.
#F* -------------------------------------------------------------------
#G* Please see the accompanying LICENSE file for further information. 
#H* -------------------------------------------------------------------
#I* Additional authors of this source file include:
#-*
#-* NOTE: Based on code by John E. Grayson which was in turn 
#-* based on code written by Doug Hellmann. 
#Z* -------------------------------------------------------------------

# this section is devoted to making sure that Tkinter variables which
# correspond to Menu-displayed settings are kept synchronized with
# PyMOL

from Tkinter import *
import Pmw
from pymol import cmd
import pymol.setting

pm = cmd

class Setting:

   def __init__(self):
   
      self.ray_trace_frames = IntVar()
      self.ray_trace_frames.set(int(cmd.get_setting_legacy('ray_trace_frames')))
      
      self.cache_frames = IntVar()
      self.cache_frames.set(int(cmd.get_setting_legacy('ray_trace_frames')))

      self.ortho = IntVar()
      self.ortho.set(int(cmd.get_setting_legacy('orthoscopic')))

      self.antialias = IntVar()
      self.antialias.set(int(cmd.get_setting_legacy('antialias')))

      self.all_states = IntVar()
      self.all_states.set(int(cmd.get_setting_legacy('all_states')))

      self.line_smooth = IntVar()
      self.line_smooth.set(int(cmd.get_setting_legacy('line_smooth')))

      self.overlay = IntVar()
      self.overlay.set(int(cmd.get_setting_legacy('overlay')))

      self.valence = IntVar()
      self.valence.set(int(cmd.get_setting_legacy('valence')))

      self.auto_zoom = IntVar()
      self.auto_zoom.set(int(cmd.get_setting_legacy('auto_zoom')))

      self.static_singletons = IntVar()
      self.static_singletons.set(int(cmd.get_setting_legacy('static_singletons')))

      self.backface_cull = IntVar()
      self.backface_cull.set(int(cmd.get_setting_legacy('backface_cull')))

      self.depth_cue = IntVar()
      self.depth_cue.set(int(cmd.get_setting_legacy('depth_cue')))

      self.specular = IntVar()
      self.specular.set(int(cmd.get_setting_legacy('specular')))

      self.cartoon_round_helices = IntVar()
      self.cartoon_round_helices.set(int(cmd.get_setting_legacy('cartoon_round_helices')))

      self.cartoon_fancy_helices = IntVar()
      self.cartoon_fancy_helices.set(int(cmd.get_setting_legacy('cartoon_fancy_helices')))

      self.cartoon_flat_sheets = IntVar()
      self.cartoon_flat_sheets.set(int(cmd.get_setting_legacy('cartoon_flat_sheets')))

      self.cartoon_fancy_sheets = IntVar()
      self.cartoon_fancy_sheets.set(int(cmd.get_setting_legacy('cartoon_fancy_sheets')))

      self.cartoon_smooth_loops = IntVar()
      self.cartoon_smooth_loops.set(int(cmd.get_setting_legacy('cartoon_smooth_loops')))

      self.xref = { 
         'ray_trace_frames':
         (lambda s,a: (cmd.set(a,("%1.0f" % s.ray_trace_frames.get())),
                       s.cache_frames.set(s.ray_trace_frames.get()),
                       s.update('cache_frames'))),
         'cache_frames'  :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.cache_frames.get())))),
         'ortho'         :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.ortho.get())))),
         'antialias'     :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.antialias.get())))),
         'valence'       :
         (lambda s,a: (cmd.set(a,("%1.2f" % (float(s.valence.get())/20.0))))),
         'all_states'    :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.all_states.get())))),
         'line_smooth'    :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.line_smooth.get())))),
         'overlay'       :
         (lambda s,a: (cmd.set(a,("%1.0f" %( s.overlay.get()*5))))),
         'auto_zoom'       :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.auto_zoom.get())))),
         'static_singletons'       :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.static_singletons.get())))),
         'backface_cull'       :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.backface_cull.get())))),
         'depth_cue'       :
         (lambda s,a: (cmd.set(a,("%1.0f" % s.depth_cue.get())))),
         'specular'       :
         (lambda s,a: (cmd.set(a,("%1.0f" % (s.specular.get()*0.8))))),

         'cartoon_round_helices'       :
         (lambda s,a: (cmd.set(a,("%1.0f" % (s.cartoon_round_helices.get()))))),
         'cartoon_fancy_helices'       :
         (lambda s,a: (cmd.set(a,("%1.0f" % (s.cartoon_fancy_helices.get()))))),
         'cartoon_flat_sheets'         :
         (lambda s,a: (cmd.set(a,("%1.0f" % (s.cartoon_flat_sheets.get()))))),
         'cartoon_fancy_sheets'        :
         (lambda s,a: (cmd.set(a,("%1.0f" % (s.cartoon_fancy_sheets.get()))))),
         'cartoon_smooth_loops'        :
         (lambda s,a: (cmd.set(a,("%1.0f" % (s.cartoon_smooth_loops.get()))))),
         
         }

      self.update_code = {
         'ray_trace_frames':
         (lambda s,t: (s.ray_trace_frames.set(int(t[1][0])))),
         'cache_frames':
         (lambda s,t: (s.cache_frames.set(int(t[1][0])))),
         'orthoscopic':
         (lambda s,t: (s.ortho.set(int(t[1][0])))),
         'all_states':
         (lambda s,t: (s.all_states.set(int(t[1][0])))),
         'line_smooth':
         (lambda s,t: (s.line_smooth.set(int(t[1][0])))),
         'overlay':
         (lambda s,t: (s.overlay.set(t[1][0]!=0))),
         'valence':
         (lambda s,t: (s.valence.set(t[1][0]>0.0))),
         'auto_zoom':
         (lambda s,t: (s.auto_zoom.set(int(t[1][0])))), 
         'static_singletons':
         (lambda s,t: (s.static_singletons.set(int(t[1][0])))), 
         'backface_cull':
         (lambda s,t: (s.backface_cull.set(int(t[1][0])))), 
         'depth_cue'       :
         (lambda s,t: (s.depth_cue.set(int(t[1][0])))), 
         'specular'       :
         (lambda s,t: (s.specular.set(t[1][0]>0.0))), 
         'cartoon_round_helices':
         (lambda s,t: (s.cartoon_round_helices.set(t[1][0]!=0))),
         'cartoon_fancy_helices':
         (lambda s,t: (s.cartoon_fancy_helices.set(t[1][0]!=0))),
         'cartoon_flat_sheets':
         (lambda s,t: (s.cartoon_flat_sheets.set(t[1][0]!=0))),
         'cartoon_fancy_sheets':
         (lambda s,t: (s.cartoon_fancy_sheets.set(t[1][0]!=0))),
         'cartoon_smooth_loops':
         (lambda s,t: (s.cartoon_smooth_loops.set(t[1][0]!=0))),
        }
      self.active_list = [
         pymol.setting._get_index("ray_trace_frames"),
         pymol.setting._get_index("cache_frames"),
         pymol.setting._get_index("orthoscopic"),
         pymol.setting._get_index("antialias"),
         pymol.setting._get_index("all_states"),
         pymol.setting._get_index("overlay"),
         pymol.setting._get_index("line_smooth"),
         pymol.setting._get_index("valence"),
         pymol.setting._get_index("auto_zoom"),
         pymol.setting._get_index("static_singletons"),
         pymol.setting._get_index("backface_cull"),
         pymol.setting._get_index("depth_cue"),
         pymol.setting._get_index("specular"),
         pymol.setting._get_index("cartoon_round_helices"),
         pymol.setting._get_index("cartoon_fancy_helices"),
         pymol.setting._get_index("cartoon_flat_sheets"),
         pymol.setting._get_index("cartoon_fancy_sheets"),
         pymol.setting._get_index("cartoon_smooth_loops"),         
         ]

      self.active_dict = {}
      for a in self.active_list:
         self.active_dict[a] = pymol.setting._get_name(a)
         
   def update(self,sttng):
      set_fn = self.xref[sttng]
      set_fn(self,sttng)

   def refresh(self): # get any settings changes from PyMOL and update menus
      lst = cmd.get_setting_updates()
      for a in lst:
         if a in self.active_list:
            name = self.active_dict[a]
            if self.update_code.has_key(name):
               code = self.update_code[name]
               if code!=None:
                  tup = cmd.get_setting_tuple(name,'',0)
                  if tup!=None:
                     apply(code,(self,tup))
      
