Name: libgbinder

Version: 1.1.53
Release: 0
Summary: Binder client library
License: BSD
URL: https://github.com/mer-hybris/libgbinder
Source: %{name}-%{version}.tar.bz2

%define glib_version 2.32
%define libglibutil_version 1.0.83

BuildRequires: pkgconfig(glib-2.0) >= %{glib_version}
BuildRequires: pkgconfig(libglibutil) >= %{libglibutil_version}
BuildRequires: pkgconfig
BuildRequires: bison
BuildRequires: flex

# license macro requires rpm >= 4.11
BuildRequires: pkgconfig(rpm)
%define license_support %(pkg-config --exists 'rpm >= 4.11'; echo $?)

# make_build macro appeared in rpm 4.12
%{!?make_build:%define make_build make %{_smp_mflags}}

# openSUSE workaround
%if 0%{?suse_version} > 0
%define libname %{name}%{so_ver}
%define so_ver %(echo %{version} | cut -d. -f1)
%description
C interfaces for Android binder

%package -n %{libname}
Summary: Runtime library for %{name}
Provides: %{name} = %{version}
%else
%define libname %{name}
%endif

Requires: glib2 >= %{glib_version}
Requires: libglibutil >= %{libglibutil_version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description -n %{libname}
C interfaces for Android binder

%package devel
Summary: Development library for %{name}
Requires: %{libname} = %{version}
Requires: pkgconfig(glib-2.0) >= %{glib_version}

%description devel
This package contains the development library for %{name}.

%prep
%setup -q

%build
%make_build LIBDIR=%{_libdir} KEEP_SYMBOLS=1 release pkgconfig
%make_build -C tools KEEP_SYMBOLS=1 release

%install
make LIBDIR=%{_libdir} DESTDIR=%{buildroot} install-dev
make -C tools DESTDIR=%{buildroot} install

%check
make -C unit test

%post -n %{libname} -p /sbin/ldconfig

%postun -n %{libname} -p /sbin/ldconfig

%files -n %{libname}
%{_libdir}/%{name}.so.*
%if %{license_support} == 0
%license LICENSE
%endif

%files devel
%dir %{_includedir}/gbinder
%{_libdir}/pkgconfig/*.pc
%{_libdir}/%{name}.so
%{_includedir}/gbinder/*.h

# Tools

%package tools
Summary: Binder tools
%if 0%{?suse_version} == 0
Requires: %{libname} >= %{version}
%endif

%description tools
Binder command line utilities

%files tools
%{_bindir}/binder-bridge
%{_bindir}/binder-list
%{_bindir}/binder-ping
%{_bindir}/binder-call
