#
# Basic chempy types
#

class Atom:

   defaults = {
      'symbol'              : 'X',
      'name'                : '',
      'resn'                : 'UNK',
      'resn_code'           : 'X',
      'resi'                : '1',
      'resi_number'         : 1,
      'b'                   : 0.0,
      'q'                   : 1.0,
      'alt'                 : '',
      'hetatm'              : 1,
      'segi'                : '',
      'chain'               : '',
      'coord'               : [9999.999,9999.999,9999.999],
      'formal_charge'       : 0.0,
      'partial_charge'      : 0.0,
# Flags
      'flags'               : 0,
# Force-fields
      'numeric_type'        : -9999,
      'text_type'           : 'UNK',
# MDL Mol-files
      'stereo'              : 0,
# Macromodel files
      'color_code'          : 2,
      }
   
   def __getattr__(self,attr):
      if Atom.defaults.has_key(attr):
         return Atom.defaults[attr]
      else:
         raise AttributeError(attr)
      
   def has(self,attr):
      return self.__dict__.has_key(attr) 

   def in_same_residue(self,other):
      if self.resi == other.resi:
         if self.chain == other.chain:
            if self.segi == other.segi:
               return 1
      return 0

   def new_in_residue(self):
      newat = Atom()
      if self.has('segi'):        newat.segi        = self.segi
      if self.has('chain'):       newat.chain       = self.chain
      if self.has('resn'):        newat.resn        = self.resn
      if self.has('resn_code'):   newat.resn_code   = self.resn_code
      if self.has('resi'):        newat.resi        = self.resi
      if self.has('resi_number'): newat.resi_number = self.resi_number
      if self.has('hetatm'):      newat.hetatm      = self.hetatm
      return newat

   def __cmp__(self,other):
      if self.segi == other.segi:
         if self.chain == other.chain:
            if self.resi_number == other.resi_number:
               if self.resi == other.resi:
                  if self.symbol == other.symbol:
                     if self.name == other.name:
                        return cmp(id(self),id(other))
                     else:
                        return cmp(self.name,other.name)
                  else:
                     return cmp(self.symbol,other.symbol)
               else:
                  return cmp(self.resi,other.resi)
            else:
               return cmp(self.resi_number,other.resi_number)               
         else:
            return cmp(self.chain,other.chain)
      else:
         return cmp(self.segi,other.segi)
      
class Bond:

   defaults = {
      'order'           : 1,
      'stereo'          : 0
      }

   def __getattr__(self,attr):
      if Bond.defaults.has_key(attr):
         return Bond.defaults[attr]
      else:
         raise AttributeError(attr)
      
   def has(self,attr):
      return self.__dict__.has_key(attr) 

class Molecule:

   defaults = {
      'dim_code'        : '3D',
      'title'           : 'untitled',
      'comments'        : '',
      'chiral'          : 1
      }

   def __getattr__(self,attr):
      if Molecule.defaults.has_key(attr):
         return Molecule.defaults[attr]
      else:
         raise AttributeError(attr)
      
   def has(self,attr):
      return self.__dict__.has_key(attr) 
   
class Storage:

   def updateFromList(self,indexed,**params):
      pass
   
   def fromList(self,**params):
      return chempy.indexed()
   
   def toList(self,indexed,**params):
      return []

   def updateFromFile(self,indexed,fname,**params):
      fp = open(fname)
      result = apply(self.updateFromList,(indexed,fp.readlines()),params)
      fp.close()

   def fromFile(self,fname,**params):
      fp = open(fname)
      result = apply(self.fromList,(fp.readlines(),),params)
      fp.close()
      return result

   def toFile(self,indexed,fname,**params):
      fp = open(fname,'w')
      result = fp.writelines(apply(self.toList,(indexed,),params))
      fp.close()

