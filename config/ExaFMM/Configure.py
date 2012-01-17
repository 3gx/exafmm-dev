import config.base

import os
import re

class ExaFMMDir(object):
  dir = os.getcwd()

class ExaFMMArch(object):
  arch = 'arch-default'

class Configure(config.base.Configure):
  def __init__(self, framework):
    config.base.Configure.__init__(self, framework)
    self.Project      = 'ExaFMM'
    self.project      = self.Project.lower()
    self.PROJECT      = self.Project.upper()
    self.headerPrefix = self.PROJECT
    self.substPrefix  = self.PROJECT
    self.defineAutoconfMacros()
    self.usingBLAS = 0
    self.usingMPI  = 0
    # Fix these later
    self.dir  = ExaFMMDir()
    self.arch = ExaFMMArch()
    self.PACKAGES_INCLUDES = ''
    self.PACKAGES_LIBS     = ''
    return

  def __str2__(self):
    desc = []
    desc.append('xxx=========================================================================xxx')
    desc.append('   Configure stage complete. Now build '+self.Project+' libraries with:')
    desc.append('   make all')
    desc.append('xxx=========================================================================xxx')
    return '\n'.join(desc)+'\n'

  def setupHelp(self, help):
    import nargs
    help.addArgument(self.Project, '-prefix=<path>', nargs.Arg(None, '', 'Specify location to install '+self.Project+' (eg. /usr/local)'))
    help.addArgument(self.Project, '-load-path=<path>', nargs.Arg(None, os.path.join(self.dir.dir, 'modules'), 'Specify location of auxiliary modules'))
    help.addArgument(self.Project, '-with-shared-libraries', nargs.ArgBool(None, 0, 'Make libraries shared'))
    help.addArgument(self.Project, '-with-dynamic-loading', nargs.ArgBool(None, 0, 'Make libraries dynamic'))
    help.addArgument(self.Project, '-with-default-arch=<bool>', nargs.ArgBool(None, 1, 'Allow using the last configured arch without setting %s_ARCH' % self.PROJECT))

    help.addArgument(self.Project, '-with-device=<name>', nargs.Arg(None, 'cpu', 'Specify the device to target, e.g. cpu, gpu'))
    help.addArgument(self.Project, '-with-expansion=<name>', nargs.Arg(None, 'Cartesian', 'Specify the expansion basis, e.g. Cartesian, Spherical'))
    return

  def setupDependencies(self, framework):
    config.base.Configure.setupDependencies(self, framework)
    self.setCompilers  = framework.require('config.setCompilers',      self)
    self.compilers     = framework.require('config.compilers',         self)
    self.types         = framework.require('config.types',             self)
    self.headers       = framework.require('config.headers',           self)
    self.functions     = framework.require('config.functions',         self)
    self.libraries     = framework.require('config.libraries',         self)

    if self.usingBLAS:
      #force blaslapack to depend on scalarType so precision is set before BlasLapack is built
      self.blaslapack = framework.require('config.packages.BlasLapack', self)
      #framework.require('PETSc.utilities.scalarTypes', self.blaslapack)
      self.blaslapack.archProvider       = self.arch
      #self.blaslapack.precisionProvider = self.scalartypes
      self.blaslapack.installDirProvider = self.dir
      self.blaslapack.headerPrefix       = self.headerPrefix
    if self.usingMPI:
      self.mpi = framework.require('config.packages.MPI', self)
      self.mpi.archProvider       = self.arch
      self.mpi.languageProvider   = self.languages
      self.mpi.installDirProvider = self.dir
      self.mpi.headerPrefix       = self.headerPrefix

    self.compilers.headerPrefix = self.headerPrefix
    self.types.headerPrefix     = self.headerPrefix
    self.headers.headerPrefix   = self.headerPrefix
    self.functions.headerPrefix = self.headerPrefix
    self.libraries.headerPrefix = self.headerPrefix
    headersC = map(lambda name: name+'.h', ['malloc'])
    functions = ['drand48', 'getcwd']
    libraries1 = [(['socket', 'nsl'], 'socket'), (['fpe'], 'handle_sigfpes')]
    self.headers.headers.extend(headersC)
    self.functions.functions.extend(functions)
    self.libraries.libraries.extend(libraries1)
    return

  def defineAutoconfMacros(self):
    self.hostMacro = 'dnl Version: 2.13\ndnl Variable: host_cpu\ndnl Variable: host_vendor\ndnl Variable: host_os\nAC_CANONICAL_HOST'
    return

  def Dump(self):
    ''' Actually put the values into the configuration files '''
    # Process package dependencies
    self.framework.packages.reverse()
    includes = [os.path.join(self.dir.dir, 'include'), os.path.join(self.dir.dir, self.arch.arch, 'include')]
    libs = []
    for i in self.framework.packages:
      if i.useddirectly:
        self.addDefine('HAVE_'+i.PACKAGE, 1)  # ONLY list package if it is used directly (and not only by another package)
      if not isinstance(i.lib, list):
        i.lib = [i.lib]
      libs.extend(i.lib)
      self.addMakeMacro(i.PACKAGE+'_LIB', self.libraries.toStringNoDupes(i.lib))
      if hasattr(i, 'include'):
        if not isinstance(i.include, list):
          i.include = [i.include]
        if not i.PACKAGE.lower() == 'valgrind':
          includes.extend(i.include)
        self.addMakeMacro(i.PACKAGE+'_INCLUDE', self.headers.toStringNoDupes(i.include))

    # Sometimes we need C compiler, even if built with C++
    self.setCompilers.pushLanguage('C')
    self.addMakeMacro('CC_FLAGS',self.setCompilers.getCompilerFlags())    
    self.setCompilers.popLanguage()

    # C preprocessor values
    self.addMakeMacro('EXAFMM_CC_INCLUDES', self.headers.toStringNoDupes(includes))
    flags = self.setCompilers.CPPFLAGS
    self.device = 'cpu'
    flags += ' -D'+self.device
    self.addMakeMacro('DEVICE', self.device)
    self.expansion = 'Cartesian'
    flags += ' -D'+self.expansion
    self.addMakeMacro('EXPAND', self.expansion)
    self.addMakeMacro('CPP_FLAGS', flags)

    # compiler values
    self.setCompilers.pushLanguage('Cxx')
    self.addMakeMacro('PCC',self.setCompilers.getCompiler())
    self.addMakeMacro('PCC_FLAGS',self.setCompilers.getCompilerFlags())
    self.setCompilers.popLanguage()
    # .o or .obj 
    self.addMakeMacro('CC_SUFFIX','o')

    # executable linker values
    self.setCompilers.pushLanguage('Cxx')
    pcc_linker = self.setCompilers.getLinker()
    self.addMakeMacro('PCC_LINKER',pcc_linker)
    self.addMakeMacro('PCC_LINKER_FLAGS',self.setCompilers.getLinkerFlags())
    self.setCompilers.popLanguage()
    # '' for Unix, .exe for Windows
    self.addMakeMacro('CC_LINKER_SUFFIX','')
    self.addMakeMacro('PCC_LINKER_LIBS',self.libraries.toStringNoDupes(self.compilers.flibs+self.compilers.cxxlibs+self.compilers.LIBS.split(' ')))

    if hasattr(self.compilers, 'FC'):
      self.setCompilers.pushLanguage('FC')
      # need FPPFLAGS in config/setCompilers
      self.addDefine('HAVE_FORTRAN','1')
      self.addMakeMacro('FPP_FLAGS',self.setCompilers.CPPFLAGS)
    
      # compiler values
      self.addMakeMacro('FC_FLAGS',self.setCompilers.getCompilerFlags())
      self.setCompilers.popLanguage()
      # .o or .obj 
      self.addMakeMacro('FC_SUFFIX','o')

      # executable linker values
      self.setCompilers.pushLanguage('FC')
      # Cannot have NAG f90 as the linker - so use pcc_linker as fc_linker
      fc_linker = self.setCompilers.getLinker()
      if config.setCompilers.Configure.isNAG(fc_linker):
        self.addMakeMacro('FC_LINKER',pcc_linker)
      else:
        self.addMakeMacro('FC_LINKER',fc_linker)
      self.addMakeMacro('FC_LINKER_FLAGS',self.setCompilers.getLinkerFlags())
      # apple requires this shared library linker flag on SOME versions of the os
      if self.setCompilers.getLinkerFlags().find('-Wl,-commons,use_dylibs') > -1:
        self.addMakeMacro('DARWIN_COMMONS_USE_DYLIBS',' -Wl,-commons,use_dylibs ')
      self.setCompilers.popLanguage()

      # F90 Modules
      if self.setCompilers.fortranModuleIncludeFlag:
        self.addMakeMacro('FC_MODULE_FLAG', self.setCompilers.fortranModuleIncludeFlag)
      else: # for non-f90 compilers like g77
        self.addMakeMacro('FC_MODULE_FLAG', '-I')
      if self.setCompilers.fortranModuleIncludeFlag:
        self.addMakeMacro('FC_MODULE_OUTPUT_FLAG', self.setCompilers.fortranModuleOutputFlag)
    else:
      self.addMakeMacro('FC','')

    # shared library linker values
    self.setCompilers.pushLanguage('Cxx')
    # need to fix BuildSystem to collect these separately
    self.addMakeMacro('SL_LINKER',self.setCompilers.getLinker())
    self.addMakeMacro('SL_LINKER_FLAGS','${PCC_LINKER_FLAGS}')
    self.setCompilers.popLanguage()
    # One of 'a', 'so', 'lib', 'dll', 'dylib' (perhaps others also?) depending on the library generator and architecture
    # Note: . is not included in this macro, consistent with AR_LIB_SUFFIX
    if self.setCompilers.sharedLibraryExt == self.setCompilers.AR_LIB_SUFFIX:
      self.addMakeMacro('SL_LINKER_SUFFIX', '')
      self.addDefine('SLSUFFIX','""')
    else:
      self.addMakeMacro('SL_LINKER_SUFFIX', self.setCompilers.sharedLibraryExt)
      self.addDefine('SLSUFFIX','"'+self.setCompilers.sharedLibraryExt+'"')
      
    #SL_LINKER_LIBS is currently same as PCC_LINKER_LIBS - so simplify
    self.addMakeMacro('SL_LINKER_LIBS','${PCC_LINKER_LIBS}')
    #self.addMakeMacro('SL_LINKER_LIBS',self.libraries.toStringNoDupes(self.compilers.flibs+self.compilers.cxxlibs+self.compilers.LIBS.split(' ')))

    if not os.path.exists(os.path.join(self.dir.dir,self.arch.arch,'lib')):
      os.makedirs(os.path.join(self.dir.dir,self.arch.arch,'lib'))

    # add a makefile entry for configure options
    self.addMakeMacro('CONFIGURE_OPTIONS', self.framework.getOptionsString(['configModules', 'optionsModule']).replace('\"','\\"'))
    return

  def dumpConfigInfo(self):
    import time
    if not os.path.isdir(os.path.join(self.arch.arch, 'include')):
      os.makedirs(os.path.join(self.arch.arch,'include'))
    fd = file(os.path.join(self.arch.arch,'include', self.project+'configinfo.h'), 'w')
    fd.write('static const char *'+self.project+'configureruntime = "'+time.ctime(time.time())+'";\n')
    fd.write('static const char *'+self.project+'configureoptions = "'+self.framework.getOptionsString(['configModules', 'optionsModule']).replace('\"','\\"')+'";\n')
    fd.close()
    return

  def dumpMachineInfo(self):
    import platform
    import time
    import script
    if not os.path.isdir(os.path.join(self.arch.arch, 'include')):
      os.makedirs(os.path.join(self.arch.arch,'include'))
    fd = file(os.path.join(self.arch.arch,'include', self.project+'machineinfo.h'),'w')
    fd.write('static const char *'+self.project+'machineinfo = \"\\n\"\n')
    fd.write('\"-----------------------------------------\\n\"\n')
    fd.write('\"Libraries compiled on %s on %s \\n\"\n' % (time.ctime(time.time()), platform.node()))
    fd.write('\"Machine characteristics: %s\\n\"\n' % (platform.platform()))
    fd.write('\"Using '+self.Project+' directory: %s\\n\"\n' % (self.dir.dir))
    fd.write('\"Using '+self.Project+' arch: %s\\n\"\n' % (self.arch.arch))
    fd.write('\"-----------------------------------------\\n\";\n')
    fd.write('static const char *'+self.project+'compilerinfo = \"\\n\"\n')
    self.setCompilers.pushLanguage('Cxx')
    fd.write('\"Using C compiler: %s %s ${COPTFLAGS} ${CFLAGS}\\n\"\n' % (self.setCompilers.getCompiler(), self.setCompilers.getCompilerFlags()))
    self.setCompilers.popLanguage()
    if hasattr(self.compilers, 'FC'):
      self.setCompilers.pushLanguage('FC')
      fd.write('\"Using Fortran compiler: %s %s ${FOPTFLAGS} ${FFLAGS} %s\\n\"\n' % (self.setCompilers.getCompiler(), self.setCompilers.getCompilerFlags(), self.setCompilers.CPPFLAGS))
      self.setCompilers.popLanguage()
    fd.write('\"-----------------------------------------\\n\";\n')
    fd.write('static const char *'+self.project+'compilerflagsinfo = \"\\n\"\n')
    fd.write('\"Using include paths: %s %s %s\\n\"\n' % ('-I'+os.path.join(self.dir.dir, self.arch.arch, 'include'), '-I'+os.path.join(self.dir.dir, 'include'), self.PACKAGES_INCLUDES))
    fd.write('\"-----------------------------------------\\n\";\n')
    fd.write('static const char *'+self.project+'linkerinfo = \"\\n\"\n')
    self.setCompilers.pushLanguage('Cxx')
    fd.write('\"Using C linker: %s\\n\"\n' % (self.setCompilers.getLinker()))
    self.setCompilers.popLanguage()
    if hasattr(self.compilers, 'FC'):
      self.setCompilers.pushLanguage('FC')
      fd.write('\"Using Fortran linker: %s\\n\"\n' % (self.setCompilers.getLinker()))
      self.setCompilers.popLanguage()
    #fd.write('\"Using libraries: %s%s -L%s %s %s %s\\n\"\n' % (self.setCompilers.CSharedLinkerFlag, os.path.join(self.dir.dir, self.arch.arch, 'lib'), os.path.join(self.dir.dir, self.arch.arch, 'lib'), '-lpetscts -lpetscsnes -lpetscksp -lpetscdm -lpetscmat -lpetscvec -lpetscsys', self.PACKAGES_LIBS, self.libraries.toStringNoDupes(self.compilers.flibs+self.compilers.cxxlibs+self.compilers.LIBS.split(' '))))
    fd.write('\"-----------------------------------------\\n\";\n')
    fd.close()
    return
    
  def configureDevice(self):
    self.device = self.argDB['with-device']
    self.addMakeMacro('DEVICE', self.device)
    return
    
  def configureExpansion(self):
    self.expansion = self.argDB['with-expansion']
    self.addMakeMacro('EXPAND', self.expansion)
    return
    
  def configureDefaultArch(self):
    conffile = os.path.join('conf', 'exafmmvariables')
    if self.framework.argDB['with-default-arch']:
      fd = file(conffile, 'w')
      fd.write(self.PROJECT+'_ARCH='+self.arch.arch+'\n')
      fd.write(self.PROJECT+'_DIR='+self.dir.dir+'\n')
      fd.write('include '+os.path.join(self.dir.dir,self.arch.arch,'conf',self.project+'variables')+'\n')
      fd.close()
      self.framework.actions.addArgument(self.Project, 'Build', 'Set default architecture to '+self.arch.arch+' in '+conffile)
    elif os.path.isfile(conffile):
      try:
        os.unlink(conffile)
      except:
        raise RuntimeError('Unable to remove file '+conffile+'. Did a different user create it?')
    return

  def configureScript(self):
    '''Output a script in the conf directory which will reproduce the configuration'''
    import nargs
    import sys
    if not os.path.isdir(os.path.join(self.arch.arch, 'conf')):
      os.makedirs(os.path.join(self.arch.arch, 'conf'))
    scriptName = os.path.join(self.arch.arch, 'conf', 'reconfigure-'+self.arch.arch+'.py')
    args = dict([(nargs.Arg.parseArgument(arg)[0], arg) for arg in self.framework.clArgs])
    if 'configModules' in args:
      if nargs.Arg.parseArgument(args['configModules'])[1] == self.Project+'.Configure':
        del args['configModules']
    if 'optionsModule' in args:
      if nargs.Arg.parseArgument(args['optionsModule'])[1] == self.Project+'.compilerOptions':
        del args['optionsModule']
    if not self.PROJECT+'_ARCH' in args:
      args[self.PROJECT+'_ARCH'] = self.PROJECT+'_ARCH='+str(self.arch.arch)
    f = file(scriptName, 'w')
    f.write('#!'+sys.executable+'\n')
    f.write('if __name__ == \'__main__\':\n')
    f.write('  import sys\n')
    f.write('  import os\n')
    f.write('  sys.path.insert(0, os.path.abspath(\'config\'))\n')
    f.write('  import configure\n')
    # pretty print repr(args.values())
    f.write('  configure_options = [\n')
    for itm in args.values():
      f.write('    \''+str(itm)+'\',\n')
    f.write('  ]\n')
    f.write('  configure.ConfigurationManager('+self.Project+').configure(configure_options)\n')
    f.close()
    try:
      os.chmod(scriptName, 0775)
    except OSError, e:
      self.framework.logPrint('Unable to make reconfigure script executable:\n'+str(e))
    self.framework.actions.addArgument(self.Project, 'File creation', 'Created '+scriptName+' for automatic reconfiguration')
    return

  def configure(self):
    #if not os.path.samefile(self.dir.dir, os.getcwd()):
    #  raise RuntimeError('Wrong PETSC_DIR option specified: '+str(self.dir.dir) + '\n  Configure invoked in: '+os.path.realpath(os.getcwd()))
    #if self.framework.argDB['prefix'] and os.path.isdir(self.framework.argDB['prefix']) and os.path.samefile(self.framework.argDB['prefix'], self.dir.dir):
    #  raise RuntimeError('Incorrect option --prefix='+self.framework.argDB['prefix']+' specified. It cannot be same as PETSC_DIR!')
    self.framework.header          = self.arch.arch+'/include/'+self.project+'conf.h'
    self.framework.cHeader         = self.arch.arch+'/include/'+self.project+'fix.h'
    self.framework.makeMacroHeader = self.arch.arch+'/conf/'+self.project+'variables'
    self.framework.makeRuleHeader  = self.arch.arch+'/conf/'+self.project+'rules'
    self.executeTest(self.configureDevice)
    self.executeTest(self.configureExpansion)
    self.executeTest(self.configureDefaultArch)
    self.executeTest(self.configureScript)
    
    self.Dump()
    self.dumpConfigInfo()
    self.dumpMachineInfo()
    self.framework.log.write('================================================================================\n')
    self.logClear()
    return
