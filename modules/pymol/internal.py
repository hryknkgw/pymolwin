
import cmd
import types
import _cmd
import string
import threading
import traceback
import thread
import re
import viewing

from chempy import io

from cmd import DEFAULT_ERROR, loadable, _load2str, Shortcut, \
   is_string, is_ok

def _ray_anti_spawn(thread_info,_self=cmd):
    # WARNING: internal routine, subject to change      
    # internal routine to support multithreaded raytracing
    thread_list = []
    for a in thread_info[1:]:
        t = threading.Thread(target=_cmd.ray_anti_thread,
                                    args=(_self._COb,a))
        t.setDaemon(1)
        thread_list.append(t)
    for t in thread_list:
        t.start()
    _cmd.ray_anti_thread(_self._COb,thread_info[0])
    for t in thread_list:
        t.join()

def _ray_hash_spawn(thread_info,_self=cmd):
    # WARNING: internal routine, subject to change      
    # internal routine to support multithreaded raytracing
    thread_list = []
    for a in thread_info[1:]:
        if a != None:
            t = threading.Thread(target=_cmd.ray_hash_thread,
                                 args=(_self._COb,a))
            t.setDaemon(1)
            thread_list.append(t)
    for t in thread_list:
        t.start()
    if thread_info[0] != None:
        _cmd.ray_hash_thread(_self._COb,thread_info[0])
    for t in thread_list:
        t.join()

def _ray_spawn(thread_info,_self=cmd):
    # WARNING: internal routine, subject to change      
    # internal routine to support multithreaded raytracing
    thread_list = []
    for a in thread_info[1:]:
        t = threading.Thread(target=_cmd.ray_trace_thread,
                                    args=(_self._COb,a))
        t.setDaemon(1)
        thread_list.append(t)
    for t in thread_list:
        t.start()
    _cmd.ray_trace_thread(_self._COb,thread_info[0])
    for t in thread_list:
        t.join()

def _coordset_update_thread(list_lock,thread_info,_self=cmd):
    # WARNING: internal routine, subject to change
    while 1:
        list_lock.acquire()
        if not len(thread_info):
            list_lock.release()
            break
        else:
            info = thread_info.pop(0)
            list_lock.release()
        _cmd.coordset_update_thread(_self._COb,info)

def _coordset_update_spawn(thread_info,n_thread,_self=cmd):
    # WARNING: internal routine, subject to change
    if len(thread_info):
        list_lock = threading.Lock() # mutex for list
        thread_list = []
        for a in range(1,n_thread):
            t = threading.Thread(target=_coordset_update_thread,
                                        args=(list_lock,thread_info))
            t.setDaemon(1)
            thread_list.append(t)
        for t in thread_list:
            t.start()
        _coordset_update_thread(list_lock,thread_info)
        for t in thread_list:
            t.join()

def _object_update_thread(list_lock,thread_info,_self=cmd):
    # WARNING: internal routine, subject to change
    while 1:
        list_lock.acquire()
        if not len(thread_info):
            list_lock.release()
            break
        else:
            info = thread_info.pop(0)
            list_lock.release()
        _cmd.object_update_thread(_self._COb,info)

def _object_update_spawn(thread_info,n_thread,_self=cmd):
    # WARNING: internal routine, subject to change
    if len(thread_info):
        list_lock = threading.Lock() # mutex for list
        thread_list = []
        for a in range(1,n_thread):
            t = threading.Thread(target=_object_update_thread,
                                        args=(list_lock,thread_info))
            t.setDaemon(1)
            thread_list.append(t)
        for t in thread_list:
            t.start()
        _object_update_thread(list_lock,thread_info)
        for t in thread_list:
            t.join()

# status reporting

# do command (while API already locked)

def _do(cmmd,log=0,echo=1,_self=cmd):
    return _cmd.do(_self._COb,cmmd,log,echo)

# movie rendering

def _mpng(prefix, first=-1, last=-1, preserve=0, _self=cmd): # INTERNAL
    import sys
    # WARNING: internal routine, subject to change
    try:
        _self.lock(_self)   
        fname = prefix
        if re.search("\.png$",fname):
            fname = re.sub("\.png$","",fname)
        fname = cmd.exp_path(fname)
        r = _cmd.mpng_(_self._COb,str(fname),int(first),int(last),int(preserve))
    finally:
        _self.unlock(-1,_self)
    return r

# copy image

def _copy_image(_self=cmd,quiet=1):
    r = DEFAULT_ERROR
    try:
        _self.lock(_self)   
        r = _cmd.copy_image(_self._COb,int(quiet))
    finally:
        _self.unlock(r,_self)
    return r

# loading


def _load(oname,finfo,state,ftype,finish,discrete,
             quiet=1,multiplex=0,zoom=-1,_self=cmd):
    # WARNING: internal routine, subject to change
    # caller must already hold API lock
    # NOTE: state index assumes 1-based state
    r = DEFAULT_ERROR
    size = 0
    if ftype not in (loadable.model,loadable.brick):
        if ftype == loadable.r3d:
            import cgo
            obj = cgo.from_r3d(finfo)
            if is_ok(obj):
                r = _cmd.load_object(_self._COb,str(oname),obj,int(state)-1,loadable.cgo,
                                      int(finish),int(discrete),int(quiet),
                                      int(zoom))
            else:
                print "Load-Error: Unable to open file '%s'."%finfo
        elif ftype == loadable.cc1: # ChemDraw 3D
            obj = io.cc1.fromFile(finfo)
            if obj:
                r = _cmd.load_object(_self._COb,str(oname),obj,int(state)-1,loadable.model,
                                      int(finish),int(discrete),
                                      int(quiet),int(zoom))
        elif ftype == loadable.moe:
            try:
                # BEGIN PROPRIETARY CODE SEGMENT
                from epymol import moe

                if (string.find(finfo,":")>1):
                    moe_file = urllib.urlopen(finfo)
                else:
                    moe_file = open(finfo)
                moe_str = moe_file.read()
                moe_file.close()
                r = moe.read_moestr(moe_str,str(oname),int(state),
                                int(finish),int(discrete),int(quiet),int(zoom),_self=_self)

                # END PROPRIETARY CODE SEGMENT
            except ImportError:
                print "Error: .MOE format not supported by this PyMOL build."
                if _self._raising(-1): raise _self._pymol.CmdException

        elif ftype == loadable.mae:
            try:
                # BEGIN PROPRIETARY CODE SEGMENT
                from epymol import schrodinger

                if (string.find(finfo,":")>1):
                    mae_file = urllib.urlopen(finfo)
                else:
                    mae_file = open(finfo)
                mae_str = mae_file.read()
                mae_file.close()
                r = schrodinger.read_maestr(mae_str,str(oname),
                                            int(state),
                                            int(finish),int(discrete),
                                            int(quiet),int(zoom))

                # END PROPRIETARY CODE SEGMENT
            except ImportError:
                print "Error: .MAE format not supported by this PyMOL build."
                if _self._raising(-1): raise self._pymol.CmdException

        else:
            if ftype in _load2str.keys() and (string.find(finfo,":")>1):
                try:
                    tmp_file = urllib.urlopen(finfo)
                except:
                    print "Error: unable to open URL '%s'"%finfo
                    traceback.print_exc()
                    return 0
                finfo = tmp_file.read(tmp_file) # WARNING: will block and hang -- thread instead?
                ftype = _load2str[ftype]
                tmp_file.close()

            r = _cmd.load(_self._COb,str(oname),finfo,int(state)-1,int(ftype),
                          int(finish),int(discrete),int(quiet),
                          int(multiplex),int(zoom))
    else:
        try:
            x = io.pkl.fromFile(finfo)
            if isinstance(x,types.ListType) or isinstance(x,types.TupleType):
                for a in x:
                    r = _cmd.load_object(_self._COb,str(oname),a,int(state)-1,
                                                int(ftype),0,int(discrete),int(quiet),
                                                int(zoom))
                    if(state>0):
                        state = state + 1
                _cmd.finish_object(_self._COb,str(oname))
            else:
                r = _cmd.load_object(_self._COb,str(oname),x,
                                            int(state)-1,int(ftype),
                                            int(finish),int(discrete),
                                            int(quiet),int(zoom))
        except:
#            traceback.print_exc()
            print "Load-Error: Unable to load file '%s'." % finfo
    return r

# function keys and other specials

def _special(k,x,y,m=0,_self=cmd): # INTERNAL (invoked when special key is pressed)
    pymol=_self._pymol
    # WARNING: internal routine, subject to change
    k=int(k)
    m=int(m)
    my_special = _self.special
    if(m>0) and (m<5):
        my_special = (_self.special,
                      _self.shft_special,
                      _self.ctrl_special,
                      _self.ctsh_special,
                      _self.alt_special)[m]
    if my_special.has_key(k):
        if my_special[k][1]:
            apply(my_special[k][1],my_special[k][2],my_special[k][3])
        else:
            key = my_special[k][0]
            if(m>0) and (m<5):
                key = ('','SHFT-','CTRL-','CTSH-','ALT-')[m] + key
            if pymol._scene_dict.has_key(key): 
                _self.scene(key)
            elif pymol._view_dict.has_key(key): 
                _self.view(key)
    return None

# control keys

def _ctrl(k,_self=cmd):
    # WARNING: internal routine, subject to change
    if _self.ctrl.has_key(k):
        ck = _self.ctrl[k]
        if ck[0]!=None:
            apply(ck[0],ck[1],ck[2])
    return None

# alt keys

def _alt(k,_self=cmd):
    # WARNING: internal routine, subject to change
    if _self.alt.has_key(k):
        ak = _self.alt[k]
        if ak[0]!=None:
            apply(ak[0],ak[1],ak[2])
    return None

# writing PNG files (thread-unsafe)

def _png(a,width=0,height=0,dpi=-1.0,ray=0,quiet=1,_self=cmd):
    # INTERNAL - can only be safely called by GLUT thread 
    # WARNING: internal routine, subject to change
    try:
        _self.lock(_self)   
        fname = a
        if not re.search("\.png$",fname):
            fname = fname +".png"
        fname = cmd.exp_path(fname)
        r = _cmd.png(_self._COb,str(fname),int(width),int(height),float(dpi),int(ray),int(quiet))
    finally:
        _self.unlock(-1,_self)
    return r

# quitting (thread-specific)

def _quit(_self=cmd):
    pymol=_self._pymol
    # WARNING: internal routine, subject to change
    try:
        _self.lock(_self)
        try: # flush and close log if possible to avoid threading exception
            if pymol._log_file!=None:
                try:
                    pymol._log_file.flush()
                except:
                    pass
                pymol._log_file.close()
                del pymol._log_file
        except:
            pass
        if _self.reaper!=None:
            try:
                _self.reaper.join()
            except:
                pass
        r = _cmd.quit(_self._COb)
    finally:
        _self.unlock(-1,_self)
    return r

# screen redraws (thread-specific)

def _refresh(swap_buffers=1,_self=cmd):  # Only call with GLUT thread!
    # WARNING: internal routine, subject to change
    r = None
    try:
        _self.lock(_self)
        if thread.get_ident() == _self._pymol.glutThread:
            if swap_buffers:
                r = _cmd.refresh_now(_self._COb)
            else:
                r = _cmd.refresh(_self._COb)
        else:
            print "Error: Ignoring an unsafe call to cmd._refresh"
    finally:
        _self.unlock(-1,_self)
    return r

# stereo (platform dependent )

def _sgi_stereo(flag): # SGI-SPECIFIC - bad bad bad
    import sys
    # WARNING: internal routine, subject to change
    if sys.platform[0:4]=='irix':
        if os.path.exists("/usr/gfx/setmon"):
            if flag:
                mode = os.environ.get('PYMOL_SGI_STEREO','1024x768_96s')
                os.system("/usr/gfx/setmon -n "+mode)
            else:
                mode = os.environ.get('PYMOL_SGI_MONO','72hz')
                os.system("/usr/gfx/setmon -n "+mode)

# color alias interpretation

def _interpret_color(_self,color):
    # WARNING: internal routine, subject to change
    _validate_color_sc(_self)
    new_color = _self.color_sc.interpret(color)
    if new_color:
        if is_string(new_color):
            return new_color
        else:
            _self.color_sc.auto_err(color,'color')
    else:
        return color

def _validate_color_sc(_self=cmd):
    # WARNING: internal routine, subject to change
    if _self.color_sc == None: # update color shortcuts if needed
        lst = _self.get_color_indices()
        lst.extend([('default',-1),('auto',-2),('current',-3),('atomic',-4)])
        _self.color_sc = Shortcut(map(lambda x:x[0],lst))
        color_dict = {}
        for a in lst: color_dict[a[0]]=a[1]

def _invalidate_color_sc(_self=cmd):
    # WARNING: internal routine, subject to change
    _self.color_sc = None

def _get_color_sc(_self=cmd):
    # WARNING: internal routine, subject to change
    _validate_color_sc(_self=_self)
    return _self.color_sc

def _get_feedback(_self=cmd): # INTERNAL
    # WARNING: internal routine, subject to change
    l = []
    if _self.lock_attempt(_self):
        try:
            r = _cmd.get_feedback(_self._COb)
            while r:
                l.append(r)
                r = _cmd.get_feedback(_self._COb)
        finally:
            _self.unlock(-1,_self)
    return l
get_feedback = _get_feedback # for legacy compatibility

def _fake_drag(_self=cmd): # internal
    _self.lock(_self)
    try:
        _cmd.fake_drag(_self._COb)
    finally:
        _self.unlock(-1,_self)
    return 1

# testing tools

# for comparing floating point numbers calculated using
# different FPUs and which may show some wobble...

def _dump_floats(lst,format="%7.3f",cnt=9):
    # WARNING: internal routine, subject to change
    c = cnt
    for a in lst:
        print format%a,
        c = c -1
        if c<=0:
            print
            c=cnt
    if c!=cnt:
        print

def _dump_ufloats(lst,format="%7.3f",cnt=9):
    # WARNING: internal routine, subject to change
    c = cnt
    for a in lst:
        print format%abs(a),
        c = c -1
        if c<=0:
            print
            c=cnt
    if c!=cnt:
        print

# HUH?
def _adjust_coord(a,i,x):
    a.coord[i]=a.coord[i]+x
    return None


