Name:           analemma
Version:        %{version}
Release:        1%{?dist}
Summary:        The analemma at Orithena, a screensaver from "Anathem"
License:        MIT
Source0:        analemma-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
The Temple of Orithena from Neal Stephenson's novel Anathem: a hole in the
dome throws the sun on the floor, and at noon each day the spot of light falls
on the analemma inlaid there.

This screensaver shows the temple now, from the real date and time, then
today's noon, then a year of noons going round the figure.

On Wayland desktops it starts by itself after five minutes idle.
analemma-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/analemma

%files
%license LICENSE
%doc README.md
%{_bindir}/analemma
%{_bindir}/analemma-screensaver
%{_datadir}/applications/analemma.desktop
%{_datadir}/icons/hicolor/256x256/apps/analemma.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/analemma-screensaver.desktop
