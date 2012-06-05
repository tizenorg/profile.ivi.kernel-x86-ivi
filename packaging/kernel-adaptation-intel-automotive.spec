#
# Spec file originally created for Fedora, modified for Moblin Linux
#

Summary: The Linux kernel (the core of the Linux operating system)


# For a stable, released kernel, released_kernel should be 1. For rawhide
# and/or a kernel built from an rc snapshot, released_kernel should
# be 0.
%define released_kernel 1

# Versions of various parts

# base_sublevel is the kernel version we're starting with and patching
# on top of -- for example, 2.6.22-rc7 starts with a 2.6.21 base,
# which yields a base_sublevel of 21.

%define base_sublevel 0



## If this is a released kernel ##
%if 0%{?released_kernel}
# Do we have a 3.0.y stable update to apply?
%define stable_update 8
# 3.x.y kernel always has the stable_update digit
%define stablerev .%{stable_update}
# Set rpm version accordingly
%define rpmversion 3.%{base_sublevel}%{?stablerev}

## The not-released-kernel case ##
%else
# The next upstream release sublevel (base_sublevel+1)
%define upstream_sublevel %(expr %{base_sublevel} + 1)
# The rc snapshot level

%define rcrev 7


%if 0%{?rcrev}
%define rctag ~rc%rcrev
%endif

%if !0%{?rcrev}
%define rctag ~rc0
%endif

# Set rpm version accordingly
%define rpmversion 3.%{upstream_sublevel}%{?rctag}
%endif

# The kernel tarball/base version
%define kversion 3.%{base_sublevel}

%define make_target bzImage

%define KVERREL %{version}-%{release}
%define hdrarch %_target_cpu

%define all_x86 i386 i586 i686 %{ix86}

%define all_arm %{arm}

%define _default_patch_fuzz 0

# Per-arch tweaks

%ifarch %{all_x86}
%define image_install_path boot
%define hdrarch i386
%define kernel_image arch/x86/boot/bzImage
%endif

%ifarch x86_64
%define image_install_path boot
%define kernel_image arch/x86/boot/bzImage
%endif

%ifarch %{all_arm}
%define image_install_path boot
%define kernel_image arch/arm/boot/zImage
%define make_target zImage
%endif

ExclusiveArch: %{all_x86}

#
# Packages that need to be installed before the kernel is, because the %post
# scripts use them.
#
#%define kernel_prereq  fileutils, module-init-tools, /sbin/init, mkinitrd >= 6.0.39-1
%define kernel_prereq  /sbin/lsmod, /sbin/init

#
# This macro does requires, provides, conflicts, obsoletes for a kernel package.
#	%%kernel_reqprovconf <subpackage>
# It uses any kernel_<subpackage>_conflicts and kernel_<subpackage>_obsoletes
# macros defined above.
#
%define kernel_reqprovconf \
Provides: kernel = %{rpmversion}-%{release}\
Provides: kernel-uname-r = %{KVERREL}%{?1:-%{1}}\
Requires(pre): %{kernel_prereq}\
%{?1:%{expand:%%{?kernel_%{1}_conflicts:Conflicts: %%{kernel_%{1}_conflicts}}}}\
%{?1:%{expand:%%{?kernel_%{1}_provides:Provides: %%{kernel_%{1}_provides}}}}\
# We can't let RPM do the dependencies automatic because it'll then pick up\
# a correct but undesirable perl dependency from the module headers which\
# isn't required for the kernel proper to function\
AutoReq: no\
AutoProv: yes\
%{nil}

Name: kernel-adaptation-intel-automotive

Group: System/Kernel
License: GPLv2
URL: http://www.kernel.org/
Version: %{rpmversion}
Release: 1

%kernel_reqprovconf

#
# List the packages used during the kernel build
#
BuildRequires: module-init-tools, bash >= 2.03
BuildRequires:  findutils,  make >= 3.78
BuildRequires: linux-firmware
BuildRequires: elfutils-libelf-devel binutils-devel

Source0: ftp://ftp.kernel.org/pub/linux/kernel/v2.6/linux-%{kversion}.tar.bz2
Source1: series

Source10: COPYING.modules

Source15: merge.pl
Source20: Makefile.config

Source100: config-generic
Source104: config-adaptation-intel-automotive

# For a stable release kernel
%if 0%{?stable_update}
Patch00: patch-3.%{base_sublevel}.%{stable_update}.bz2
%endif
%if 0%{?rcrev}
Patch00: patch-3.%{upstream_sublevel}-rc%{rcrev}.bz2
%endif

# Reminder of the patch filename format:
# linux-<version it is supposed to be upstream>-<description-separated-with-dashes>.patch
#

Patch1:    0001-i2c-eg20t-back-ported-driver-from-3.2-rc-as-well-as-.patch
Patch2:    0002-gpio-ml-ioh-back-ported-the-3.2-rc-GPIO-controller-d.patch
Patch3:    0003-tsc2007-add-a-z1-threshhold-value-to-filter-noise-da.patch
Patch4:    0004-crossville-add-the-platform-code-and-register-tsc200.patch
Patch5:    0005-Added-EMGD-2032-kernel-driver-from-ECG.patch
Patch6:    0006-pch_phub-unify-the-inital-UART-clock-rate-for-EG20T-.patch
Patch7:    0007-pch_uart-set-the-uartclk-to-192MHz-after-we-configur.patch
Patch8:    0008-video_in-import-2.6.37-patch.patch 
Patch9:    0009-video_in-add-video_in-driver-and-adv7180-driver.patch 
patch10:   0010-emgd-2335-based-on-2209.patch
patch11:   0011-emgd-2348-based-on-2335.patch
patch12:   0012-emgd-2443-based-on-2348.patch
patch13:   0013-Backport-of-Linux-3.2-Smack-to-Linux-3.0.patch
Patch14:   fix-l2cap-conn-failures-for-ssp-devices.patch
Patch15:   emgd-revise-user-configid-2443.patch
Patch16:   emgd-add-nexconn-display-support-2443.patch
Patch17:   emgd-enabled-KMS-as-default-2443.patch

BuildRoot: %{_tmppath}/kernel-%{KVERREL}-root
#
# This macro creates a kernel-<subpackage>-devel package.
#	%%kernel_devel_package <subpackage> <pretty-name>
#
%define kernel_devel_package() \
%package -n kernel-%{?1:%{1}-}devel\
Summary: Development package for building kernel modules to match the %{?2:%{2} }kernel\
Group: System/Kernel\
Provides: kernel%{?1:-%{1}}-devel = %{version}-%{release}\
Provides: kernel-devel = %{version}-%{release}%{?1:-%{1}}\
Provides: kernel-devel = %{version}-%{release}%{?1:-%{1}}\
Provides: kernel-devel-uname-r = %{KVERREL}%{?1:-%{1}}\
Requires: kernel%{?1:-%{1}} = %{version}-%{release}\
Requires: hardlink \
AutoReqProv: no\
Requires(pre): /usr/bin/find\
%description -n kernel%{?1:-%{1}}-devel\
This package provides kernel headers and makefiles sufficient to build modules\
against the %{?2:%{2} }kernel package.\
%{nil}

#
# This macro creates a kernel-<subpackage> and its -devel too.
#	%%define variant_summary The Linux kernel compiled for <configuration>
#	%%kernel_variant_package [-n <pretty-name>] <subpackage>
#
%define kernel_variant_package(n:) \
%package -n kernel-%1\
Summary: %{variant_summary}\
Group: System/Kernel\
%kernel_reqprovconf\
%{nil}


%define variant_summary Kernel for Intel-based automotive platforms
%kernel_devel_package adaptation-intel-automotive
%description -n kernel-adaptation-intel-automotive
This package contains the kernel optimized for Intel-based automotive platforms


%prep

# First we unpack the kernel tarball.
# If this isn't the first make prep, we use links to the existing clean tarball
# which speeds things up quite a bit.

# Update to latest upstream.
%if 0%{?released_kernel}
%define vanillaversion 2.6.%{base_sublevel}
# released_kernel with stable_update available case
%if 0%{?stable_update}
%define vanillaversion 2.6.%{base_sublevel}.%{stable_update}
%endif
# non-released_kernel case
%else
%if 0%{?rcrev}
%define vanillaversion 2.6.%{upstream_sublevel}-rc%{rcrev}
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%endif


#
# Unpack the kernel tarbal
#
%setup -q -c
cd %{name}-%{version}


#
# The add an -rc patch if needed
#
%if 0%{?rcrev}
# patch-2.6.%{upstream_sublevel}-rc%{rcrev}.bz2
%patch00 -p1
%endif
%if 0%{?stable_update}
# patch-2.6.%{base_sublevel}.%{stable_update}.bz2
%patch00 -p1
%endif


#
# Then apply all the patches
#
# Reminder of the patch filename format:
# linux-<version it is supposed to be upstream>-<description-separated-with-dashes>.patch
#

%patch1 -p1
%patch2 -p1
%patch3 -p1
%patch4 -p1
%patch5 -p1
%patch6 -p1
%patch7 -p1
%patch8 -p1
%patch9 -p1
%patch10 -p1
%patch11 -p1
%patch12 -p1
%patch13 -p1
%patch14 -p1
%patch15 -p1
%patch16 -p1
%patch17 -p1


# Drop some necessary files from the source dir into the buildroot
cp $RPM_SOURCE_DIR/config-* .
cp %{SOURCE15} .
cp %{SOURCE20} .

# Dynamically generate kernel .config files from config-* files
make -f %{SOURCE20} VERSION=%{version}.config configs

# Any further pre-build tree manipulations happen here.
chmod +x scripts/checkpatch.pl

cp %{SOURCE10} Documentation/

mkdir configs

#
# We want to run the config checks of all configurations for all architectures always.
# That way, developers immediately found out if they forget to enable not-their-native
# architecture. It's cheap to run anyway.
#

# now run oldconfig over all the config files
for i in kernel-*.config
do
 
  mv $i .config
  Arch="x86"

  #get ARCH from .config file for other platforms
  if [ `cat .config | grep -c CONFIG_ARM=y` -eq 1 ]; then
    Arch="arm"
  fi

  # make oldconfig > /dev/null
  echo Doing $i

  make ARCH=$Arch listnewconfig &> /tmp/configs
  export conf=`cat /tmp/configs | grep CONFIG | wc -l`
  echo CONF is $conf
  if [ $conf -gt 0 ]; then
	  make ARCH=$Arch listnewconfig  
	  #exit 1
  fi
  make ARCH=$Arch oldconfig > /dev/null
  echo "# $Arch" > configs/$i
  cat .config >> configs/$i
done


#
# get rid of unwanted files resulting from patch fuzz
# (not that we can have any)
#
find . \( -name "*.orig" -o -name "*~" \) -exec rm -f {} \; >/dev/null

cd ..


###
### build
###
%build


cp_vmlinux()
{
  eu-strip --remove-comment -o "$2" "$1"
}

BuildKernel() {
    MakeTarget=$1
    KernelImage=$2
    TargetArch=$3
    Flavour=$4
    InstallName=${5:-vmlinuz}

    # Pick the right config file for the kernel we're building
    Config=kernel${Flavour:+-${Flavour}}.config
    DevelDir=/usr/src/kernels/%{KVERREL}${Flavour:+-${Flavour}}

    # When the bootable image is just the ELF kernel, strip it.
    # We already copy the unstripped file into the debuginfo package.
    if [ "$KernelImage" = vmlinux ]; then
      CopyKernel=cp_vmlinux
    else
      CopyKernel=cp
    fi

    KernelVer=%{version}-%{release}${Flavour:+-${Flavour}}
    ExtraVer=%{?rctag}-%{release}${Flavour:+-${Flavour}}
    Arch="x86"
%ifarch %{all_arm}
    Arch="arm"
%endif


    if [ "$Arch" = "$TargetArch" ]; then
        echo BUILDING A KERNEL FOR ${Flavour} %{_target_cpu}... ${KernelVer}
        echo USING ARCH=$Arch

        # make sure EXTRAVERSION says what we want it to say
        perl -p -i -e "s/^EXTRAVERSION.*/EXTRAVERSION = ${ExtraVer}/" Makefile

        # and now to start the build process

        make -s mrproper
        cp configs/$Config .config

        make -s ARCH=$Arch oldconfig > /dev/null
        make -s CONFIG_DEBUG_SECTION_MISMATCH=y ARCH=$Arch %{?_smp_mflags} $MakeTarget %{?sparse_mflags}
        make -s CONFIG_DEBUG_SECTION_MISMATCH=y ARCH=$Arch %{?_smp_mflags} modules %{?sparse_mflags} || exit 1

        # Start installing the results
        mkdir -p $RPM_BUILD_ROOT/%{image_install_path}
        install -m 644 .config $RPM_BUILD_ROOT/boot/config-$KernelVer
        install -m 644 System.map $RPM_BUILD_ROOT/boot/System.map-$KernelVer
        touch $RPM_BUILD_ROOT/boot/initrd-$KernelVer.img
        if [ -f arch/$Arch/boot/zImage.stub ]; then
          cp arch/$Arch/boot/zImage.stub $RPM_BUILD_ROOT/%{image_install_path}/zImage.stub-$KernelVer || :
        fi
        $CopyKernel $KernelImage \
        		$RPM_BUILD_ROOT/%{image_install_path}/$InstallName-$KernelVer
        chmod 755 $RPM_BUILD_ROOT/%{image_install_path}/$InstallName-$KernelVer

        mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer
        make -s ARCH=$Arch INSTALL_MOD_PATH=$RPM_BUILD_ROOT modules_install KERNELRELEASE=$KernelVer
%ifnarch %{all_arm}
        make -s ARCH=$Arch INSTALL_MOD_PATH=$RPM_BUILD_ROOT vdso_install KERNELRELEASE=$KernelVer
%endif

        # And save the headers/makefiles etc for building modules against
        #
        # This all looks scary, but the end result is supposed to be:
        # * all arch relevant include/ files
        # * all Makefile/Kconfig files
        # * all script/ files

        rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
        rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/source
        mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
        (cd $RPM_BUILD_ROOT/lib/modules/$KernelVer ; ln -s build source)
        # dirs for additional modules per module-init-tools, kbuild/modules.txt
        # first copy everything
        cp --parents `find  -type f -name "Makefile*" -o -name "Kconfig*"` $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
        cp Module.symvers $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
        cp System.map $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
        if [ -s Module.markers ]; then
          cp Module.markers $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
        fi
        # then drop all but the needed Makefiles/Kconfig files
        rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/Documentation
        rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts
        rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
        cp .config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
        cp -a scripts $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
        if [ -d arch/%{_arch}/scripts ]; then
          cp -a arch/%{_arch}/scripts $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/%{_arch} || :
        fi
        if [ -f arch/%{_arch}/*lds ]; then
          cp -a arch/%{_arch}/*lds $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/%{_arch}/ || :
        fi
        rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts/*.o
        rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts/*/*.o
        cp -a --parents arch/$Arch/include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
%ifarch %{all_arm}
       cp -a --parents arch/arm/mach-*/include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
       cp -a --parents arch/arm/plat-*/include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
%endif
        mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
        cd include
        cp -a acpi asm-generic config crypto drm generated keys linux math-emu media mtd net pcmcia rdma rxrpc scsi sound video trace $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include

        # Make sure the Makefile and version.h have a matching timestamp so that
        # external modules can be built
        touch -r $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/Makefile $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/linux/version.h
        touch -r $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/.config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/linux/autoconf.h
        # Copy .config to include/config/auto.conf so "make prepare" is unnecessary.
        cp $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/.config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/config/auto.conf
        cd ..

        #
        # save the vmlinux file for kernel debugging into the kernel-*-devel rpm
        #

        cp vmlinux $RPM_BUILD_ROOT/lib/modules/$KernelVer

        find $RPM_BUILD_ROOT/lib/modules/$KernelVer -name "*.ko" -type f >modnames

        # mark modules executable so that strip-to-file can strip them
        xargs --no-run-if-empty chmod u+x < modnames

        # Generate a list of modules for block and networking.

        fgrep /drivers/ modnames | xargs --no-run-if-empty nm -upA |
        sed -n 's,^.*/\([^/]*\.ko\):  *U \(.*\)$,\1 \2,p' > drivers.undef

        collect_modules_list()
        {
          sed -r -n -e "s/^([^ ]+) \\.?($2)\$/\\1/p" drivers.undef |
          LC_ALL=C sort -u > $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.$1
        }

        collect_modules_list networking \
        			 'register_netdev|ieee80211_register_hw|usbnet_probe'
        collect_modules_list block \
        			 'ata_scsi_ioctl|scsi_add_host|blk_init_queue|register_mtd_blktrans'

        # remove files that will be auto generated by depmod at rpm -i time
        for i in alias ccwmap dep ieee1394map inputmap isapnpmap ofmap pcimap seriomap symbols usbmap
        do
          rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.$i
        done

        # Move the devel headers out of the root file system
        mkdir -p $RPM_BUILD_ROOT/usr/src/kernels
        mv $RPM_BUILD_ROOT/lib/modules/$KernelVer/build $RPM_BUILD_ROOT/$DevelDir
        ln -sf ../../..$DevelDir $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    fi
}

###
# DO it...
###

# prepare directories
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/boot

cd %{name}-%{version}

BuildKernel %make_target %kernel_image x86 adaptation-intel-automotive

###
### install
###

%define install  %{?_enable_debug_packages:%{?buildsubdir:%{debug_package}}}\
%%install


%install

cd %{name}-%{version}

rm -rf $RPM_BUILD_ROOT/lib/firmware


###
### clean
###

%clean
rm -rf $RPM_BUILD_ROOT

###
### scripts
###

#
# This macro defines a %%post script for a kernel*-devel package.
#	%%kernel_devel_post <subpackage>
#
%define kernel_devel_post() \
%{expand:%%post -n kernel-%{?1:%{1}-}devel}\
if [ -x /usr/sbin/hardlink ]\
then\
    (cd /usr/src/kernels/%{KVERREL}%{?1:-%{1}} &&\
     /usr/bin/find . -type f | while read f; do\
       hardlink -c /usr/src/kernels/*/$f $f\
     done)\
fi\
%{nil}

# This macro defines a %%posttrans script for a kernel package.
#	%%kernel_variant_posttrans [-v <subpackage>] [-s <s> -r <r>] <mkinitrd-args>
# More text can follow to go at the end of this variant's %%post.
#
%define kernel_variant_posttrans(s:r:v:) \
%{expand:%%posttrans -n kernel-%{?-v*}}\
%{nil}

#
# This macro defines a %%post script for a kernel package and its devel package.
#	%%kernel_variant_post [-v <subpackage>] [-s <s> -r <r>] <mkinitrd-args>
# More text can follow to go at the end of this variant's %%post.
#
%define kernel_variant_post(s:r:v:) \
%{expand:%%kernel_devel_post %{?-v*}}\
%{expand:%%kernel_variant_posttrans %{?-v*}}\
%{expand:%%post -n kernel-%{?-v*}}\
%{nil}

#
# This macro defines a %%preun script for a kernel package.
#	%%kernel_variant_preun <subpackage>
#
%define kernel_variant_preun() \
%{expand:%%preun -n kernel-%{?1}}\
%{nil}


%ifarch %all_x86

%kernel_variant_preun adaptation-intel-automotive
%kernel_variant_post -v adaptation-intel-automotive

%endif


###
### file lists
###



#
# This macro defines the %%files sections for a kernel package
# and its devel packages.
#	%%kernel_variant_files [-k vmlinux] [-a <extra-files-glob>] [-e <extra-nonbinary>] <condition> <subpackage>
#
%define kernel_variant_files(a:e:k:) \
%ifarch %{1}\
%{expand:%%files -n kernel%{?2:-%{2}}}\
%defattr(-,root,root)\
/%{image_install_path}/%{?-k:%{-k*}}%{!?-k:vmlinuz}-%{KVERREL}%{?2:-%{2}}\
/boot/System.map-%{KVERREL}%{?2:-%{2}}\
#/boot/symvers-%{KVERREL}%{?2:-%{2}}.gz\
/boot/config-%{KVERREL}%{?2:-%{2}}\
%{?-a:%{-a*}}\
%dir /lib/modules/%{KVERREL}%{?2:-%{2}}\
/lib/modules/%{KVERREL}%{?2:-%{2}}/kernel\
/lib/modules/%{KVERREL}%{?2:-%{2}}/build\
/lib/modules/%{KVERREL}%{?2:-%{2}}/source\
%ifnarch %{all_arm}\
/lib/modules/%{KVERREL}%{?2:-%{2}}/vdso\
%endif\
/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.block\
#/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.devname\
/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.softdep\
/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.dep.bin\
/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.alias.bin\
/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.symbols.bin\
/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.networking\
/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.order\
/lib/modules/%{KVERREL}%{?2:-%{2}}/modules.builtin*\
%ghost /boot/initrd-%{KVERREL}%{?2:-%{2}}.img\
%{?-e:%{-e*}}\
%{expand:%%files -n kernel-%{?2:%{2}-}devel}\
%defattr(-,root,root)\
%verify(not mtime) /usr/src/kernels/%{KVERREL}%{?2:-%{2}}\
/lib/modules/%{KVERREL}%{?2:-%{2}}/vmlinux \
%endif\
%{nil}


%kernel_variant_files %all_x86 adaptation-intel-automotive
