# Third-party notices

This candidate combines project code under MPL-2.0 with separately licensed third-party source. Original notices remain in the source files. These texts also accompany locally staged Windows packages.

Versions below are local evidence. A version macro or fetched license text does not prove an exact upstream source checkout. File hashes and license acquisition records are in `third_party/manifest.json`.

## Components

| Component | Local evidence | License | License text |
|---|---|---|---|
| [PicoSHA2](https://github.com/okdshin/PicoSHA2) | 2026-10-16 upstream snapshot; SHA-256 pinned in manifest | MIT | [LICENSE](third_party/picosha2/LICENSE) |
| [spdlog](https://github.com/gabime/spdlog) | 1.17.0 (local header) | MIT | [LICENSE](src/lib/spdlog/LICENSE) |
| [fmt (bundled by spdlog)](https://github.com/fmtlib/fmt) | 12.1.0 (local header) | MIT | [fmt.license.rst](src/lib/spdlog/include/spdlog/fmt/bundled/fmt.license.rst) |
| [libcurl](https://github.com/curl/curl) | 8.22.1-DEV (local header) | curl | [COPYING](src/lib/curl/COPYING), [BSD-4-Clause-UC.txt](src/lib/curl/LICENSES/BSD-4-Clause-UC.txt), [ISC.txt](src/lib/curl/LICENSES/ISC.txt) |
| [MARISA](https://github.com/s-yata/marisa-trie) | legacy subset; exact import revision unknown | BSD-2-Clause | [COPYING](src/lib/marisa-trie/COPYING) |
| [OpenCC](https://github.com/BYVoid/OpenCC) | modified legacy subset; exact import revision unknown | Apache-2.0 | [LICENSE](src/lib/opencc/LICENSE) |
| [RapidJSON](https://github.com/Tencent/rapidjson) | 1.1.0 (directory/header) | MIT (see additional terms in license.txt) | [license.txt](src/lib/opencc/deps/rapidjson-1.1.0/license.txt) |
| [TCLAP](https://tclap.sourceforge.net/) | 1.2.2 (directory) | MIT | [COPYING](src/lib/opencc/deps/tclap-1.2.2/COPYING) |
| [Darts-clone](https://github.com/s-yata/darts-clone) | 0.32 (local header) | BSD-2-Clause | [COPYING](src/lib/opencc/deps/darts-clone/COPYING) |
| [Boost subset](https://github.com/boostorg/boost) | 1.88.0 (local header) | BSL-1.0 | [LICENSE_1_0.txt](src/lib/boost/LICENSE_1_0.txt) |
| [XZ Embedded](https://github.com/tukaani-project/xz-embedded) | local source snapshot; exact import revision unknown | 0BSD | [COPYING](src/lib/xz-embedded/COPYING) |
| [Stephan Brumme hash-library (MD5)](https://github.com/stbrumme/hash-library) | local MD5 snapshot; exact revision unknown | Zlib | [LICENSE](src/common/thirdparty/hash-library/LICENSE) |
| [parallel-hashmap / Abseil-derived code](https://github.com/greg7mdp/parallel-hashmap) | 1.0.0 (macro; not proof of upstream revision) | Apache-2.0 | [LICENSE](src/common/thirdparty/parallel_hashmap/LICENSE) |
| [SimpleIni](https://github.com/brofield/simpleini) | 4.22 (local header) | MIT | [LICENCE.txt](src/common/thirdparty/simpleini/LICENCE.txt) |
| [Unicode ConvertUTF](https://github.com/brofield/simpleini) | Copyright 2001-2004 Unicode, Inc. | LicenseRef-Unicode-ConvertUTF | [Unicode-ConvertUTF.txt](third_party/licenses/Unicode-ConvertUTF.txt) |
| [xxHash](https://github.com/Cyan4973/xxHash) | 0.8.3 (local header) | BSD-2-Clause | [LICENSE](src/common/thirdparty/xxhash/LICENSE) |
| [Windows Implementation Library](https://github.com/microsoft/wil) | local header subset; exact import revision unknown | MIT | [LICENSE](src/platform/windows/common/wil/LICENSE) |
| [Windows Template Library](https://wtl.sourceforge.io/) | 10.0 (local header) | MS-PL | [MS-PL.txt](src/platform/windows/common/wtl10/MS-PL.txt) |
| [miniz](https://github.com/richgel999/miniz) | 3.1.2 / 77d0dce8627735138c51770d1799a1ef48f2117d | MIT | [LICENSE](third_party/miniz/LICENSE) |
| [PacketVideo-derived UTF conversion](https://android.googlesource.com/platform/external/opencore/) | Copyright 1998-2009 PacketVideo; adapted local file | Apache-2.0 | [Apache-2.0.txt](third_party/licenses/Apache-2.0.txt) |
| [NSIS installer runtime / external tool](https://nsis.sourceforge.io/Docs/AppendixI.html) | 3.08 used in local packaging tests | Zlib for chosen installer/stub; see full COPYING | [NSIS-COPYING.txt](third_party/licenses/NSIS-COPYING.txt) |

MARISA is distributed under its BSD-2-Clause alternative. Optional OpenCC dependency headers remain licensed even when not linked into the current targets.

SimpleIni ConvertUTF files retain the Unicode 2001–2004 notice. The project-adapted PacketVideo UTF conversion file remains Apache-2.0 and is excluded from the project-owned MPL scope.

OpenCC dictionaries and other official dictionary/model data are outside the source candidate; their own licenses still apply when supplied in a distribution.

## Source attributions

### spdlog

- Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
- Copyright(c) 2015 Gabi Melman.
- Copyright (c) 2012 - present, Victor Zverovich
- Copyright Paul Dreik 2019
- Copyright (c) 2018 - present, Victor Zverovich and fmt contributors
- Copyright (c) 2012 - present, Victor Zverovich and fmt contributors
- Copyright (c) 2012 - 2016, Victor Zverovich
- The above copyright notice and this permission notice shall be
- NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
- without including the above copyright and permission notices.
- Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
- Copyright(c) 2016 Gabi Melman.
- Copyright(c) 2016-2018 Gabi Melman.
- Copyright(c) 2016 Alexander Dalshov & spdlog contributors.
- Copyright(c) 2015-present, Gabi Melman, mguludag and spdlog contributors.
- Copyright(c) 2019 ZVYAGIN.Alexander@gmail.com

### fmt (bundled by spdlog)

- Copyright (c) 2012 - present, Victor Zverovich
- Copyright Paul Dreik 2019
- Copyright (c) 2018 - present, Victor Zverovich and fmt contributors
- Copyright (c) 2012 - present, Victor Zverovich and fmt contributors
- Copyright (c) 2012 - 2016, Victor Zverovich
- The above copyright notice and this permission notice shall be
- NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
- without including the above copyright and permission notices.
- Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors

### libcurl

- Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
- are also available at https://curl.se/docs/copyright.html.
- Copyright (C) Jeroen Ooms <jeroenooms@gmail.com>
- Copyright (C) James Fuller, <jim@webcomposite.com>, et al.
- This is the global package copyright */
- define LIBCURL_COPYRIGHT "Daniel Stenberg, <daniel@haxx.se>."
- Copyright (C) Linus Nielsen Feltzing, <linus@haxx.se>
- Copyright (C) Florin Petriuc, <petriuc.florin@gmail.com>
- Copyright (C) Evgeny Grin (Karlson2k), <k2k@narod.ru>.
- Copyright (C) 1996-2022  Internet Software Consortium.
- copyright notice and this permission notice appear in all copies.
- Copyright (c) Internet Software Consortium.
- Copyright (C) Steve Holme, <steve_holme@hotmail.com>.
- Copyright (C) Björn Stenberg, <bjorn@haxx.se>
- Copyright (C) Howard Chu, <hyc@openldap.org>
- Copyright (C) Bill Nagel <wnagel@tycoint.com>, Exacq Technologies
- Copyright (C) Markus Moeller, <markus_moeller@compuserve.com>
- Copyright (C) Simon Josefsson, <simon@josefsson.org>, et al.
- Copyright (C) Red Hat, Inc.
- Copyright (C) Jan Venekamp, <jan@venekamp.net>
- Copyright (C) Hoi-Ho Chan, <hoiho.chan@gmail.com>
- Copyright (C) Jacob Hoffman-Andrews, <github@hoffman-andrews.com>
- Copyright (C) kpcyrd, <kpcyrd@archlinux.org>
- Copyright (C) Daniel McCarney, <daniel@binaryparadox.net>
- Copyright (C) Marc Hoersken, <info@marc-hoersken.de>
- Copyright (C) Mark Salisbury, <mark.salisbury@hp.com>
- Copyright (C) Marc Hoersken, <info@marc-hoersken.de>, et al.

### OpenCC

- Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip. All rights reserved.
- Copyright (c) 2006-2013 Alexander Chemeris
- 1. Redistributions of source code must retain the above copyright notice,
- 2. Redistributions in binary form must reproduce the above copyright
- All Tencent Modifications are Copyright (C) 2015 THL A29 Limited.
- Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip-> All rights reserved->
- Copyright (c) 2003, Michael E. Smoot .
- Copyright (c) 2004, Michael E. Smoot, Daniel Aarno .
- Copyright (c) 2017 Google Inc.
- THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
- Copyright (c) 2007, Daniel Aarno, Michael E. Smoot .
- Copyright (c) 2004, Michael E. Smoot, Daniel Aarno.
- Copyright (c) 2004, Michael E. Smoot
- Copyright (c) 2005, Michael E. Smoot
- Copyright (c) 2005, Michael E. Smoot, Daniel Aarno, Erik Zeek.
- Copyright (c) 2005, Michael E. Smoot .
- Copyright (c) 2003, Michael E. Smoot.
- Copyright (c) 2006, Oliver Kiddle
- Copyright 2010-2020 Carbo Kuo <byvoid@byvoid.com>
- Copyright 2010-2014 Carbo Kuo <byvoid@byvoid.com>
- Copyright 2010-2017 Carbo Kuo <byvoid@byvoid.com>
- Copyright 2010-2021 Carbo Kuo <byvoid@byvoid.com>
- Copyright 2020 Carbo Kuo <byvoid@byvoid.com>
- Copyright 2021 Carbo Kuo <byvoid@byvoid.com>
- Copyright 2015-2020 Carbo Kuo <byvoid@byvoid.com>
- Copyright 2015 Carbo Kuo <byvoid@byvoid.com>
- Copyright 2013 Carbo Kuo <byvoid@byvoid.com>

### RapidJSON

- Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip. All rights reserved.
- Copyright (c) 2006-2013 Alexander Chemeris
- 1. Redistributions of source code must retain the above copyright notice,
- 2. Redistributions in binary form must reproduce the above copyright
- All Tencent Modifications are Copyright (C) 2015 THL A29 Limited.
- Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip-> All rights reserved->

### TCLAP

- Copyright (c) 2003, Michael E. Smoot .
- Copyright (c) 2004, Michael E. Smoot, Daniel Aarno .
- Copyright (c) 2017 Google Inc.
- THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
- Copyright (c) 2007, Daniel Aarno, Michael E. Smoot .
- Copyright (c) 2004, Michael E. Smoot, Daniel Aarno.
- Copyright (c) 2004, Michael E. Smoot
- Copyright (c) 2005, Michael E. Smoot
- Copyright (c) 2005, Michael E. Smoot, Daniel Aarno, Erik Zeek.
- Copyright (c) 2005, Michael E. Smoot .
- Copyright (c) 2003, Michael E. Smoot.
- Copyright (c) 2006, Oliver Kiddle

### Boost subset

- Copyright 2019, 2021 Peter Dimov
- Copyright (c) 2001, 2002 Peter Dimov and Multi Media Ltd.
- Copyright (c) 2007, 2014 Peter Dimov
- Copyright (c) Beman Dawes 2011
- Copyright (c) 2015 Ion Gaztanaga
- (C) Copyright John Maddock 2003.
- (c) Copyright John Maddock 2003
- Copyright John Maddock 2002-21.
- Copyright John Maddock 2002-4.
- (C) Copyright John Maddock 2001 - 2003.
- (C) Copyright David Abrahams 2002 - 2003.
- (C) Copyright Aleksey Gurtovoy 2002.
- (C) Copyright Douglas Gregor 2010
- Copyright 2021 Peter Dimov
- (C) Copyright John Maddock 2001.
- (C) Copyright Douglas Gregor 2001.
- (C) Copyright Peter Dimov 2001.
- (C) Copyright Aleksey Gurtovoy 2003.
- (C) Copyright Beman Dawes 2003.
- (C) Copyright Jens Maurer 2003.
- (C) Copyright John Maddock 2001 - 2002.
- (C) Copyright Jens Maurer 2001.
- (C) Copyright David Abrahams 2002.
- (C) Copyright Markus Schoepflin 2005.
- Copyright 2011 John Maddock
- Copyright 2013, 2017-2018 Cray, Inc.
- (C) Copyright Brian Kuhl 2016.
- Copyright (C) Christof Meerwald 2003
- Copyright (C) Dan Watkins 2003
- (C) Copyright Darin Adler 2001 - 2002.
- (C) Copyright Jens Maurer 2001 - 2002.
- (C) Copyright Beman Dawes 2001 - 2003.
- (C) Copyright Douglas Gregor 2002.
- (C) Copyright Synge Todo 2003.
- (C) Copyright John Maddock 2006.
- (C) Copyright Jens Maurer 2001 - 2003.
- (C) Copyright Toon Coralpen 2003.
- (C) Copyright Boris Gubenko 2006 - 2007.
- (C) Copyright John Maddock 2001-8.
- (C) Copyright Aleksey Gurtovoy 2002 - 2003.
- (C) Copyright Guillaume Melquiond 2002 - 2003.
- (C) Copyright Martin Wille 2003.
- (C) Copyright Darin Adler 2001.
- (C) Copyright David Abrahams 2001 - 2002.
- (C) Copyright Stefan Slapeta 2004.
- (C) Copyright Eric Jourdanneau, Joel Falcou 2010
- (C) Copyright Bryce Lelbach 2011
- (C) Copyright Noel Belcourt 2007.
- Copyright 2017, NVIDIA CORPORATION.
- (C) Copyright Peter Dimov 2002.
- (C) Copyright Toon Coralpen 2001 - 2003.
- (C) Copyright Lie-Quan Lee 2001.
- (C) Copyright Markus Schoepflin 2002 - 2003.
- (C) Copyright Beman Dawes 2002 - 2003.
- Copyright (c) 2017 Dynatrace
- (C) Copyright Guillaume Melquiond 2003.
- Copyright (c) 2001-2003 John Maddock
- Copyright (c) 2001 Darin Adler
- Copyright (c) 2001 Peter Dimov
- Copyright (c) 2002 Bill Kempf
- Copyright (c) 2002 Jens Maurer
- Copyright (c) 2002-2003 David Abrahams
- Copyright (c) 2003 Gennaro Prota
- Copyright (c) 2003 Eric Friedman
- Copyright (c) 2010 Eric Jourdanneau, Joel Falcou
- Copyright 2017 Peter Dimov.
- Copyright 2001 John Maddock.
- (C) Copyright John Maddock 2008.
- (C) Copyright John Maddock 2005.
- (C) Copyright John Maddock 2002.
- Copyright Nuxi, https://nuxi.nl/ 2015.
- (C) Copyright John Maddock 2011.
- (C) Copyright Jessica Hamilton 2014.
- (C) Copyright Bill Kempf 2002.
- (C) Copyright Jim Douglas 2005.
- (C) Copyright Yuriy Krasnoschek 2009.
- (C) Copyright Artyom Beilis 2010.
- (C) Copyright Dustin Spicuzza 2009.
- (C) Copyright John Maddock 2020.
- (C) Copyright Bill Kempf 2001.
- (C) Copyright Rene Rivera 2005.
- (C) Copyright John Maddock 2002 - 2003.
- (C) Copyright Jens Maurer 2002 - 2003.
- (C) Copyright Christopher Jefferson 2011.
- (C) Copyright David Abrahams 2003.
- (C) Copyright Boris Gubenko 2007.
- Copyright John Maddock 2008
- Copyright David Abrahams 2002.
- (C) Copyright Pablo Halpern 2009. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2011-2013. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2005-2014. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2014-2015. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2008-2013. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2014-2014.
- (C) Copyright Ion Gaztanaga 2005-2013. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2012-2013. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2014-2014. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2017-2017. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2005-2013.
- (C) Copyright Ion Gaztanaga 2005-2015. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2016-2016. Distributed under the Boost
- (C) Copyright Gennaro Prota 2003 - 2004.
- (C) Copyright Ion Gaztanaga  2014-2015
- (C) Copyright John Maddock 2000.
- (C) Copyright Ion Gaztanaga 2005-2015.
- (C) Copyright Ion Gaztanaga  2013-2013
- Copyright 2021-2023 Peter Dimov
- Copyright 2022 Peter Dimov
- Copyright 2005-2009 Daniel James.
- Copyright 2021 Peter Dimov.
- Copyright 2022, 2023 Peter Dimov
- Copyright 2005-2014 Daniel James.
- Copyright 2021, 2022 Peter Dimov.
- Copyright 2017, 2018 Peter Dimov.
- Copyright 2022 Peter Dimov.
- Copyright 2017, 2022 Peter Dimov.
- Copyright (C) 2002 Brad King (brad.king@kitware.com)
- Copyright (C) 2002, 2008, 2013 Peter Dimov
- Copyright (C) 2017 Glen Joseph Fernandes (glenjofe@gmail.com)
- Copyright 2020-2022 Glen Joseph Fernandes
- Copyright 2021 Glen Joseph Fernandes
- Copyright 2020 Peter Dimov
- Copyright 2018 Glen Joseph Fernandes
- Copyright 2003 (c) The Trustees of Indiana University.
- Copyright (C) 2007, 2008 Steven Watanabe, Joseph Gauterin, Niels Dekker
- Copyright (C) 2023 Andrey Semashev
- (C) Copyright 2004 Pavel Vozenilek.
- (C) Copyright Beman Dawes 1999-2003. Distributed under the Boost
- Copyright 2019 Glen Joseph Fernandes
- Copyright 2017-2021 Glen Joseph Fernandes
- Copyright 2023 Peter Dimov
- Copyright 2001, 2004, 2011 Daryle Walker.
- \copyright  Boost Software License, version 1.0
- (C) Copyright Beman Dawes 1999.
- (C) Copyright Jens Mauer 2001
- (C) Copyright John Maddock 2001
- Copyright 2002-2018 Peter Dimov
- Copyright 2020, 2021 Peter Dimov
- Copyright 2005 Peter Dimov
- Copyright 2018, 2019 Andrey Semashev
- Copyright (c) 2006-2009 Emil Dotchevski and Reverge Studios, Inc.
- (C) Copyright Ion Gaztanaga 2005-2012. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2008-2012. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2007-2012. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2009-2012. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2006-2012
- (C) Copyright Markus Schoepflin 2007
- (C) Copyright Bryce Lelbach 2010
- (C) Copyright Ion Gaztanaga 2020-2021. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2012-2012. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2006-2012. Distributed under the Boost
- (C) Copyright Stephen Cleary 2000.
- (C) Copyright Ion Gaztanaga 2007-2012.
- (C) Copyright Ion Gaztanaga 2005-2012.
- (C) Copyright Ion Gaztanaga 2010-2012.
- (C) Copyright Ion Gaztanaga 2005-2016.
- Copyright (c) 2002 Peter Dimov and Multi Media Ltd.
- Copyright (c) 2008 Peter Dimov
- (C) Copyright Ion Gaztanaga 2010-2012. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2007-2014. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2021-2021.
- Copyright (C) 2002 Beman Dawes
- Copyright (C) 2001 Dietmar Kuehl
- Copyright 2024 Braden Ganetsky
- (C) Copyright Peter Dimov and Multi Media Ltd. 2001, 2002, 2003
- (C) Copyright Ion Gaztanaga 2006. Distributed under the Boost
- (C) Copyright Peter Dimov 2004-2005
- Copyright (c) 2001, 2002, 2003 Peter Dimov and Multi Media Ltd.
- Copyright 2004-2005 Peter Dimov
- Copyright 2007-2012 Ion Gaztanaga
- Copyright 2006      Ion Gaztanaga
- (C) Copyright Peter Dimov 2002
- (C) Copyright Peter Dimov 2001, 2002
- (C) Copyright Greg Colvin and Beman Dawes 1998, 1999.
- (C) Copyright Peter Dimov 2001, 2002, 2003
- (C) Copyright Ion Gaztanaga 2006-2012.
- (C) Copyright Ion Gaztanaga 2006-2014. Distributed under the Boost
- Copyright (c) 1998
- provided that the above copyright notice appear in all copies and
- that both that copyright notice and this permission notice appear
- Copyright (C) 2011 Vicente J. Botet Escriba
- Copyright (C) 2021 Ion Gaztanaga
- Copyright (C) 2001-2003
- (C) Copyright Howard Hinnant 2007-2010. Distributed under the Boost
- (C) Copyright Peter Dimov 2008.
- (C) Copyright Ion Gaztanaga 2013-2013. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2011-2012. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2023-2024. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2009. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2007-2013
- (C) Copyright Daniel K. O. 2005.
- (C) Copyright Ion Gaztanaga 2007-2014
- (C) Copyright Ion Gaztanaga  2013-2014
- (C) Copyright Ion Gaztanaga  2007-2021
- (C) Copyright Daniel Steck   2021
- (C) Copyright Olaf Krzikalla 2004-2006.
- (C) Copyright Ion Gaztanaga  2006-2014
- (C) Copyright Ion Gaztanaga  2014-2014
- (C) Copyright Ion Gaztanaga  2006-2013
- (C) Copyright Ion Gaztanaga  2007-2014
- (C) Copyright Joaquin M Lopez Munoz  2006-2013
- (C) Copyright Ion Gaztanaga          2014-2014
- (C) Copyright Ion Gaztanaga  2009-2013.
- Copyright 2007 Alexandre Courpron
- provided that this copyright notice appears on all copies of the software.
- domain. The author hereby disclaims copyright to this source code.
- Copyright 2021 Ion Gaztanaga
- Copyright 2024 Ion Gaztanaga
- (C) Copyright Ion Gaztanaga        2006-2014
- (C) Copyright Microsoft Corporation  2014
- (C) Copyright Ion Gaztanaga  2007-2013
- (C) Copyright Ion Gaztanaga  2006-2013.
- (C) Copyright Ion Gaztanaga 2015-2015. Distributed under the Boost
- (C) Copyright Ion Gaztanaga 2017-2021. Distributed under the Boost
- (C) Copyright Ion Gaztanaga  2010-2013
- (C) Copyright Ion Gaztanaga 2011-2014. Distributed under the Boost
- (C) Copyright Ion Gaztanaga  2006-2014.
- Here is the copyright notice of the original file containing the splay code:
- Copyright (c) 2004 Ralf Mattethat
- is granted provided this copyright notice appears in all copies.
- (C) Copyright John maddock 1999.
- (C) Copyright 2007, 2008 Steven Watanabe, Joseph Gauterin, Niels Dekker
- (C) Copyright Ion Gaztanaga 2015-2016.
- (C) Copyright Ion Gaztanaga 2017-2018.
- (C) Copyright Ion Gaztanaga 2017-2018. Distributed under the Boost
- (C) Copyright Orson Peters  2017.
- The original copyright statement is pasted here for completeness:
- Copyright (c) 2015 Orson Peters
- (C) Copyright Ion Gaztanaga 2022-2022.
- (C) Copyright Ion Gaztanaga 2017-2017.
- (C) Copyright Ion Gaztanaga 2012-2016.
- (C) Copyright Ion Gaztanaga 2012-2012.
- (C) Copyright Ion Gaztanaga 2012-2015.
- (C) Copyright Ion Gaztanaga 2015-2015.
- (C) Copyright Ion Gaztanaga 2010-2016.
- (C) Copyright Ion Gaztanaga 2014-2017. Distributed under the Boost
- (C) Copyright Ion Gaztanaga  2017-2017
- (C) Copyright David Abrahams, Vicente Botet 2009.
- (C) Copyright Ion Gaztanaga 2009-2012.
- Copyright 2015-2019 Peter Dimov
- Copyright 2016, 2018, 2019 Peter Dimov.
- Copyright 2015-2017 Peter Dimov.
- Copyright 2015-2019 Peter Dimov.
- Copyright 2015, 2016 Peter Dimov.
- Copyright 2015-2020, 2023 Peter Dimov
- Copyright 2015-2023 Peter Dimov.
- Copyright 2015 Peter Dimov.
- Copyright 2015, 2017, 2019 Peter Dimov.
- Copyright 2015, 2019, 2024 Peter Dimov.
- Copyright 2015-2020 Peter Dimov.
- Copyright 2019 Peter Dimov
- Copyright Rene Rivera 2013
- Copyright Rene Rivera 2014
- Copyright Rene Rivera 2011-2012
- Copyright Rene Rivera 2008-2015
- Copyright Franz Detro 2014
- Copyright Rene Rivera 2015
- Copyright Rene Rivera 2015-2019
- Copyright 2017 James E. King, III
- Copyright Ruslan Baratov 2017
- Copyright Rene Rivera 2017
- Copyright (c) Microsoft Corporation 2014
- Copyright James E. King III, 2017
- Copyright Rene Rivera 2013-2015
- Copyright Rene Rivera 2005-2016
- Copyright (c) 2002, 2018-2022 Peter Dimov
- Copyright (c) 2008-2009 Emil Dotchevski and Reverge Studios, Inc.
- Copyright 2024 Braden Ganetsky.
- Copyright 2023 Joaquin M Lopez Munoz.
- Copyright (C) 2022-2024 Joaquin M Lopez Munoz.
- Copyright (C) 2022 Christian Mazakas
- Copyright (C) 2003-2004 Jeremy B. Maitin-Shepard.
- Copyright (C) 2005-2016 Daniel James
- Copyright (C) 2022-2023 Christian Mazakas
- Copyright (C) 2024 Braden Ganetsky
- Copyright 2022 Joaquin M Lopez Munoz.
- Copyright (C) 2023 Christian Mazakas
- Copyright (C) 2022 Joaquin M Lopez Munoz.
- Copyright 2023 Christian Mazakas
- Copyright (C) 2023 Braden Ganetsky
- Copyright (C) 2005-2011 Daniel James.
- Copyright (C) 2024 Joaquin M Lopez Munoz.
- Copyright (C) 2008-2011 Daniel James.
- Copyright (C) 2005-2008 Daniel James.
- (C) Copyright John maddock 1999. Distributed under the Boost
- Copyright 2010 Vicente J. Botet Escriba
- Copyright 2015-2018 Andrey Semashev
- Copyright 2013, 2017 Andrey Semashev
- Copyright Andrey Semashev 2020.
- Copyright 2015 Andrey Semashev
- Copyright 2014 Renato Tegon Forti, Antony Polukhin
- Copyright 2015, 2020 Andrey Semashev
- Copyright 2015 Antony Polukhin
- Copyright 2016-2018 Andrey Semashev
- Copyright 2016 Jorge Lodos
- Copyright 2020 Andrey Semashev
- Copyright 2016 Klemens D. Morgenstern
- Copyright 2016 Andrey Semashev
- Copyright 2015, 2017 Andrey Semashev

### Stephan Brumme hash-library (MD5)

- Copyright (c) 2014,2015 Stephan Brumme. All rights reserved.
- Copyright (c) 2014 Stephan Brumme. All rights reserved.

### parallel-hashmap / Abseil-derived code

- Copyright (c) 2019, Gregory Popovitch - greg7mdp@gmail.com
- Copyright 2018 The Abseil Authors.

### SimpleIni

- Copyright 2001-2004 Unicode, Inc.

### Unicode ConvertUTF

- Copyright 2001-2004 Unicode, Inc.

### xxHash

- Copyright (C) 2012-2023 Yann Collet
- Redistributions of source code must retain the above copyright
- copyright notice, this list of conditions and the following disclaimer
- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
- A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT

### Windows Implementation Library

- Copyright (c) Microsoft. All rights reserved.

### Windows Template Library

- Copyright (C) Microsoft Corporation, WTL Team. All rights reserved.

### miniz

- Copyright 2013-2014 RAD Game Tools and Valve Software
- Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
- The above copyright notice and this permission notice shall be included in
- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
- Copyright 2016 Martin Raiber

### PacketVideo-derived UTF conversion

- Copyright (C) 1998-2009 PacketVideo

## Local changes and source availability

Vendored libraries use project CMake definitions and may include platform adaptations. The original import revision of older copies was not recorded; the shipped local source is identified by file hashes. Do not present these copies as unmodified upstream releases.

The application-wide MPL statement does not replace any license above. Preserve component notices and provide the corresponding project source when distributing MPL-covered binaries.

Windows SDK and system DLLs are external build/runtime requirements. No SDK, DbgHelp, former zip-utils or WebView2 binary is redistributed by this candidate. Python model-builder dependencies and NSIS are acquired separately under their own terms.
