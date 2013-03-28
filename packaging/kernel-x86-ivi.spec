#
# Spec written for Tizen Mobile, some bits and pieces originate
# from MeeGo/Moblin/Fedora
#

%define upstream_version 3.8.3
%define variant x86-ivi
%define kernel_version %{version}-%{release}
%define kernel_full_version %{version}-%{release}-%{variant}
%define kernel_arch x86

Name: kernel-%{variant}
Summary: The Linux kernel
Group: System Environment/Kernel
License: GPLv2
URL: http://www.kernel.org/
Version: %{upstream_version}
Release: 10
BuildRequires: module-init-tools
BuildRequires: findutils
BuildRequires: elfutils-libelf-devel
BuildRequires: binutils-devel
BuildRequires: which
# net-tools provides the 'hostname' utility which kernel build wants
BuildRequires: net-tools
# The below is required for building perf
BuildRequires: flex
BuildRequires: bison
BuildRequires: elfutils-devel
BuildRequires: python-devel
ExclusiveArch: %{ix86}

Provides: kernel = %{version}-%{release}
Provides: kernel-uname-r = %{kernel_full_version}
Requires(post): /bin/ln
Requires(postun): /bin/ln
Requires(postun): /bin/sed
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
Group: Development/System
Provides: kernel-devel = %{kernel_full_version}
Provides: kernel-devel-uname-r = %{kernel_full_version}
Requires(post): /usr/bin/find
Requires: %{name} = %{version}-%{release}
AutoReqProv: no

%description devel
This package provides kernel headers and makefiles sufficient to build modules
against the %{variant} kernel package.


%package -n perf
Summary: The 'perf' performance counter tool
Group: System Environment/Kernel
Provides: perf = %{kernel_full_version}
Requires: %{name} = %{version}-%{release}

%description -n perf
This package provides the "perf" tool that can be used to monitor performance
counter events as well as various kernel internal events.



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

# Build perf
make -s -C tools/lib/traceevent ARCH=%{kernel_arch} %{?_smp_mflags}
make -s -C tools/perf WERROR=0 ARCH=%{kernel_arch}

# Build kernel and modules
make -s ARCH=%{kernel_arch} ivi_defconfig
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
# However, the entire 'include/config' directory is not needed
rm -rf %{buildroot}/lib/modules/%{kernel_full_version}/build/include/config

# Save the vmlinux file for kernel debugging into the devel package
cp vmlinux %{buildroot}/lib/modules/%{kernel_full_version}

# Mark modules executable so that strip-to-file can strip them
find %{buildroot}/lib/modules/%{kernel_full_version} -name "*.ko" -type f | xargs --no-run-if-empty chmod 755

# Move the devel headers out of the root file system
install -d %{buildroot}/usr/src/kernels
mv %{buildroot}/lib/modules/%{kernel_full_version}/build %{buildroot}/usr/src/kernels/%{kernel_full_version}

ln -sf ../../../usr/src/kernels/%{kernel_full_version} %{buildroot}/lib/modules/%{kernel_full_version}/build

# Install perf
install -d %{buildroot}
make -s -C tools/perf DESTDIR=%{buildroot} install
install -d  %{buildroot}/usr/bin
install -d  %{buildroot}/usr/libexec
mv %{buildroot}/bin/* %{buildroot}/usr/bin/
mv %{buildroot}/libexec/* %{buildroot}/usr/libexec/
rm %{buildroot}/etc/bash_completion.d/perf



###
### CLEAN
###

%clean
rm -rf %{buildroot}



###
### SCRIPTS
###

%post
ln -sf vmlinuz-%{kernel_full_version} /boot/vmlinuz

%post devel
if [ -x /usr/sbin/hardlink ]; then
	cd /usr/src/kernels/%{kernel_full_version}
	/usr/bin/find . -type f | while read f; do
		hardlink -c /usr/src/kernels/*/$f $f
	done
fi

%postun
if [ $1 -gt 1 ]; then
	# There is another kernel, change the /boot/vmlinuz symlink to the
	# previously installed kernel.
	prev_ver="$(rpm -q --last kernel-%{variant} | sed -e "s/kernel-%{variant}-\([^ ]*\).*/\1/g" | sed -e "/^%{kernel_version}$/d" | sed -n -e "1p")"
	ln -sf vmlinuz-$prev_ver-%{variant} /boot/vmlinuz
else
	rm /boot/vmlinuz
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


%files -n perf
/usr/bin/perf
/usr/libexec/perf-core
