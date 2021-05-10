Name: libgbinder

Version: 1.1.10
Release: 0
Summary: Binder client library
License: BSD
URL: https://github.com/mer-hybris/libgbinder
Source: %{name}-%{version}.tar.bz2

%define libglibutil_version 1.0.52

BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libglibutil) >= %{libglibutil_version}
Requires: libglibutil >= %{libglibutil_version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
C interfaces for Android binder

%package devel
Summary: Development library for %{name}
Requires: %{name} = %{version}
Requires: pkgconfig

%description devel
This package contains the development library for %{name}.

%prep
%setup -q

%build
make %{_smp_mflags} LIBDIR=%{_libdir} KEEP_SYMBOLS=1 release pkgconfig
make -C test/binder-bridge KEEP_SYMBOLS=1 release
make -C test/binder-list KEEP_SYMBOLS=1 release
make -C test/binder-ping KEEP_SYMBOLS=1 release

%install
rm -rf %{buildroot}
make LIBDIR=%{_libdir} DESTDIR=%{buildroot} install-dev
make -C test/binder-bridge DESTDIR=%{buildroot} install
make -C test/binder-list DESTDIR=%{buildroot} install
make -C test/binder-ping DESTDIR=%{buildroot} install

%check
make -C unit test

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/%{name}.so.*

%files devel
%defattr(-,root,root,-)
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
