# OE: conditional switches
#(ie. use with rpm --rebuild):
#	--with diet	Compile convertfs against dietlibc

%define build_diet 0

# commandline overrides:
# rpm -ba|--rebuild --with 'xxx'
%{?_with_diet: %{expand: %%define build_diet 1}}

%define date	20050113
%define version	0.%{date}
%define fdate	%(date -d %date +%d%b%Y | tr [:upper:] [:lower:])

Summary:	Convert one file system to another
Name:		convertfs
Version:	0.%{date}
Release:	5
Source0:	http://tzukanov.narod.ru/convertfs/%{name}-%{fdate}.tar.gz
# lynx -dump -nolist http://tzukanov.narod.ru/convertfs/ > README
Source1:	README.lzma
License:	GPLv2
Group:		System/Kernel and hardware
URL:		http://tzukanov.narod.ru/convertfs/
BuildRoot:	%{_tmppath}/%{name}-%{version}-root

%if %{build_diet}
BuildRequires:	dietlibc-devel >= 0.20-1mdk
%endif

%description
ConvertFS is a very simple but extremely powerful toolset which
allows users to convert one file system to another. It works for
converting virtually any filesystem type to virtually any one as
long as they are both block-oriented and supported by Linux for
read/write, and as long as primary filesystem supports sparse
files. 

 * devclone  -  Utility to make clone of the block device (sparse
                file of the same size).
 * devremap  -  Core of the toolset - block relocation utility.
 * prepindex -  Utility to prepare index (list of raw blocks) of
                filesystem image.

%prep
%setup -q -n %{name}
lzcat %{SOURCE1} > README

%build
%if %{build_diet}
    # OE: use the power of dietlibc
    for i in devclone devremap prepindex; do
	diet gcc -s -static -o $i $i.c -Os
    done	
%else
    %make CFLAGS="%{optflags}"
%endif

%install
[ -n "%{buildroot}" -a "%{buildroot}" != / ] && rm -rf %{buildroot}

install -d %{buildroot}/sbin
install -m755 devclone %{buildroot}/sbin/
install -m755 devremap %{buildroot}/sbin/
install -m755 prepindex %{buildroot}/sbin/

%clean
[ -n "%{buildroot}" -a "%{buildroot}" != / ] && rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc README contrib test convertfs_dumb
/sbin/devclone
/sbin/devremap
/sbin/prepindex



%changelog
* Mon Dec 07 2009 Jérôme Brenier <incubusss@mandriva.org> 0.20050113-3mdv2010.1
+ Revision: 474285
- rebuild

* Wed Sep 02 2009 Thierry Vignaud <tv@mandriva.org> 0.20050113-2mdv2010.1
+ Revision: 424967
- rebuild

* Thu May 15 2008 Adam Williamson <awilliamson@mandriva.org> 0.20050113-1mdv2009.0
+ Revision: 207916
- use cunning macro to handle the date properly
- new license policy
- new release

  + Olivier Blin <oblin@mandriva.com>
    - restore BuildRoot

  + Thierry Vignaud <tv@mandriva.org>
    - kill re-definition of %%buildroot on Pixel's request

* Tue Dec 04 2007 Thierry Vignaud <tv@mandriva.org> 0.20020318-4mdv2008.1
+ Revision: 114992
- use %%mkrel
- import convertfs


* Tue May 10 2005 Oden Eriksson <oeriksson@mandriva.com> 0.20020318-4mdk
- deactivate dietlibc build as it won't build on x86_64

* Fri Oct 15 2004 Oden Eriksson <oeriksson@mandrakesoft.com> 0.20020318-3mdk
- rpmbuildupdated

* Mon Aug 04 2003 Per �yvind Karlsen <peroyvind@linux-mandrake.com> 0.20020318-2mdk
- rebuild

* Sun Jul 27 2003 Oden Eriksson <oden.eriksson@kvikkjokk.net> 0.20020318-1mdk
- initial cooker contrib
- use spec file magic to compile convertfs against dietlibc, maybe something
  for the rescue stuff?
