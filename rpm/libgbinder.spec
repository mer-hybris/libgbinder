Name: libgbinder

Version: 1.1.40
Release: 0
Summary: Binder client library
License: BSD
URL: https://github.com/mer-hybris/libgbinder
Source: %{name}-%{version}.tar.bz2

%define glib_version 2.32
%define libglibutil_version 1.0.52

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

Requires: glib2 >= %{glib_version}
Requires: libglibutil >= %{libglibutil_version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
C interfaces for Android binder

%package devel
Summary: Development library for %{name}
Requires: %{name} = %{version}
Requires: pkgconfig(glib-2.0) >= %{glib_version}

%description devel
This package contains the development library for %{name}.

%prep
%setup -q

%build
%make_build LIBDIR=%{_libdir} KEEP_SYMBOLS=1 release pkgconfig
%make_build -C test/binder-bridge -j1 KEEP_SYMBOLS=1 release
%make_build -C test/binder-list -j1 KEEP_SYMBOLS=1 release
%make_build -C test/binder-ping -j1 KEEP_SYMBOLS=1 release
%make_build -C test/binder-call -j1 KEEP_SYMBOLS=1 release

%install
make LIBDIR=%{_libdir} DESTDIR=%{buildroot} install-dev
make -C test/binder-bridge DESTDIR=%{buildroot} install
make -C test/binder-list DESTDIR=%{buildroot} install
make -C test/binder-ping DESTDIR=%{buildroot} install
make -C test/binder-call DESTDIR=%{buildroot} install

%check
make -C unit test

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/%{name}.so.*
%if %{license_support} == 0
%license LICENSE
%endif

%files devel
%defattr(-,root,root,-)
%dir %{_includedir}/gbinder
%{_libdir}/pkgconfig/*.pc
%{_libdir}/%{name}.so
%{_includedir}/gbinder/*.h

# Tools

%package tools
Summary: Binder tools
Requires: %{name} >= %{version}

%description tools
Binder command line utilities

%files tools
%defattr(-,root,root,-)
%{_bindir}/binder-bridge
%{_bindir}/binder-list
%{_bindir}/binder-ping
%{_bindir}/binder-call
