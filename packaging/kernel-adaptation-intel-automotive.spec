#
# Spec written for Tizen Mobile, some bits and pieces originate
# from MeeGo/Moblin/Fedora
#

%define upstream_version 3.4.4
%define variant adaptation-intel-automotive
%define kernel_full_version %{version}-%{release}-%{variant}
%define kernel_arch x86

Name: kernel-%{variant}
Summary: The Linux kernel
Group: System/Kernel
License: GPLv2
URL: http://www.kernel.org/
Version: %{upstream_version}
Release: 3
BuildRequires: module-init-tools
BuildRequires: findutils
BuildRequires: elfutils-libelf-devel
BuildRequires: binutils-devel
BuildRequires: which
BuildRequires: linux-firmware
ExclusiveArch: %{ix86}

Provides: kernel = %{version}-%{release}
Provides: kernel-uname-r = %{kernel_full_version}
Provides: k%{kernel_full_version}
# We can't let RPM do the dependencies automatic because it'll then pick up
# a correct but undesirable perl dependency from the module headers which
# isn't required for the kernel proper to function
AutoReq: no
AutoProv: yes

Source0: %{name}-%{version}.tar.bz2


%description
This package contains the Tizen IVI Linux kernel


%package devel
Summary: Development package for building kernel modules to match the %{variant} kernel
Group: System/Kernel
Provides: kernel-devel = %{kernel_full_version}
Provides: kernel-devel-uname-r = %{kernel_full_version}
Requires(pre): /usr/bin/find
Requires: %{name} = %{version}-%{release}
AutoReqProv: no

%description devel
This package provides kernel headers and makefiles sufficient to build modules
against the %{variant} kernel package.


###
### PREP
###
%prep
# Unpack the kernel tarbal
%setup -q -n %{name}-%{version}


###
### BUILD
###
%build
# Make sure EXTRAVERSION says what we want it to say
sed -i "s/^EXTRAVERSION.*/EXTRAVERSION = -%{release}-%{variant}/" Makefile

# Build kernel and modules
make -s ARCH=%{kernel_arch} ivi_gen_defconfig
make -s ARCH=%{kernel_arch} %{?_smp_mflags} bzImage
make -s ARCH=%{kernel_arch} %{?_smp_mflags} modules



###
### INSTALL
###
%install
install -d %{buildroot}/boot

install -m 644 .config %{buildroot}/boot/config-%{kernel_full_version}
install -m 644 System.map %{buildroot}/boot/System.map-%{kernel_full_version}
install -m 755 arch/%{kernel_arch}/boot/bzImage %{buildroot}/boot/vmlinuz-%{kernel_full_version}
# Dummy initrd, will not be included in the actual package but needed for files
touch %{buildroot}/boot/initrd-%{kernel_full_version}.img

make -s ARCH=%{kernel_arch} INSTALL_MOD_PATH=%{buildroot} modules_install KERNELRELEASE=%{kernel_full_version}
make -s ARCH=%{kernel_arch} INSTALL_MOD_PATH=%{buildroot} vdso_install KERNELRELEASE=%{kernel_full_version}
rm -rf %{buildroot}/lib/firmware

# And save the headers/makefiles etc for building modules against
#
# This all looks scary, but the end result is supposed to be:
# * all arch relevant include/ files
# * all Makefile/Kconfig files
# * all script/ files

# Remove existing build/source links and create pristine dirs
rm %{buildroot}/lib/modules/%{kernel_full_version}/build
rm %{buildroot}/lib/modules/%{kernel_full_version}/source
install -d %{buildroot}/lib/modules/%{kernel_full_version}/build
ln -s build %{buildroot}/lib/modules/%{kernel_full_version}/source

# First, copy all dirs containing Makefile of Kconfig files
cp --parents `find  -type f -name "Makefile*" -o -name "Kconfig*"` %{buildroot}/lib/modules/%{kernel_full_version}/build
install Module.symvers %{buildroot}/lib/modules/%{kernel_full_version}/build/
install System.map %{buildroot}/lib/modules/%{kernel_full_version}/build/

# Then, drop all but the needed Makefiles/Kconfig files
rm -rf %{buildroot}/lib/modules/%{kernel_full_version}/build/Documentation
rm -rf %{buildroot}/lib/modules/%{kernel_full_version}/build/scripts
rm -rf %{buildroot}/lib/modules/%{kernel_full_version}/build/include

# Copy config and scripts
install .config %{buildroot}/lib/modules/%{kernel_full_version}/build/
cp -a scripts %{buildroot}/lib/modules/%{kernel_full_version}/build
if [ -d arch/%{kernel_arch}/scripts ]; then
    cp -a arch/%{kernel_arch}/scripts %{buildroot}/lib/modules/%{kernel_full_version}/build/arch/%{kernel_arch}/ || :
fi
if [ -f arch/%{kernel_arch}/*lds ]; then
    cp -a arch/%{kernel_arch}/*lds %{buildroot}/lib/modules/%{kernel_full_version}/build/arch/%{kernel_arch}/ || :
fi
rm -f %{buildroot}/lib/modules/%{kernel_full_version}/build/scripts/*.o
rm -f %{buildroot}/lib/modules/%{kernel_full_version}/build/scripts/*/*.o
cp -a --parents arch/%{kernel_arch}/include %{buildroot}/lib/modules/%{kernel_full_version}/build

# Copy include files
mkdir -p %{buildroot}/lib/modules/%{kernel_full_version}/build/include
find include/ -mindepth 1 -maxdepth 1 -type d | xargs -I{} cp -a {} %{buildroot}/lib/modules/%{kernel_full_version}/build/include

# Make sure the Makefile and version.h have a matching timestamp so that
# external modules can be built
touch -r %{buildroot}/lib/modules/%{kernel_full_version}/build/Makefile %{buildroot}/lib/modules/%{kernel_full_version}/build/include/linux/version.h
touch -r %{buildroot}/lib/modules/%{kernel_full_version}/build/.config %{buildroot}/lib/modules/%{kernel_full_version}/build/include/linux/autoconf.h
# Copy .config to include/config/auto.conf so "make prepare" is unnecessary.
cp %{buildroot}/lib/modules/%{kernel_full_version}/build/.config %{buildroot}/lib/modules/%{kernel_full_version}/build/include/config/auto.conf

# Save the vmlinux file for kernel debugging into the devel package
cp vmlinux %{buildroot}/lib/modules/%{kernel_full_version}

# Mark modules executable so that strip-to-file can strip them
find %{buildroot}/lib/modules/%{kernel_full_version} -name "*.ko" -type f | xargs --no-run-if-empty chmod 755

# Move the devel headers out of the root file system
install -d %{buildroot}/usr/src/kernels
mv %{buildroot}/lib/modules/%{kernel_full_version}/build %{buildroot}/usr/src/kernels/%{kernel_full_version}

ln -sf ../../../usr/src/kernels/%{kernel_full_version} %{buildroot}/lib/modules/%{kernel_full_version}/build



###
### CLEAN
###

%clean
rm -rf %{buildroot}



###
### SCRIPTS
###

%post
ln -sf vmlinuz-%{kernel_full_version} /boot/kernel

%post devel
if [ -x /usr/sbin/hardlink ]; then
    cd /usr/src/kernels/%{kernel_full_version}
    /usr/bin/find . -type f | while read f; do
        hardlink -c /usr/src/kernels/*/$f $f
    done
fi



###
### FILES
###
%files
%defattr(-,root,root)
/boot/vmlinuz-%{kernel_full_version}
/boot/System.map-%{kernel_full_version}
/boot/config-%{kernel_full_version}
%dir /lib/modules/%{kernel_full_version}
/lib/modules/%{kernel_full_version}/kernel
/lib/modules/%{kernel_full_version}/build
/lib/modules/%{kernel_full_version}/source
/lib/modules/%{kernel_full_version}/vdso
/lib/modules/%{kernel_full_version}/modules.*
%ghost /boot/initrd-%{kernel_full_version}.img


%files devel
%defattr(-,root,root)
%verify(not mtime) /usr/src/kernels/%{kernel_full_version}
/lib/modules/%{kernel_full_version}/vmlinux
