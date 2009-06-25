/* dvigen.c
   
   Copyright 2009 Taco Hoekwater <taco@luatex.org>

   This file is part of LuaTeX.

   LuaTeX is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   LuaTeX is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
   License for more details.

   You should have received a copy of the GNU General Public License along
   with LuaTeX; if not, see <http://www.gnu.org/licenses/>. */

#include "ptexlib.h"

#undef write_dvi

static const char __svn_version[] =
    "$Id$"
    "$URL$";

#include "commands.h"
#include "tokens.h"

#define mode cur_list.mode_field        /* current mode */

#define pdf_output int_par(param_pdf_output_code)
#define mag int_par(param_mag_code)
#define tracing_output int_par(param_tracing_output_code)
#define tracing_stats int_par(param_tracing_stats_code)
#define tracing_online int_par(param_tracing_online_code)
#define page_direction int_par(param_page_direction_code)       /* really direction */
#define page_width dimen_par(param_page_width_code)
#define page_height dimen_par(param_page_height_code)
#define page_left_offset dimen_par(param_page_left_offset_code)
#define page_right_offset dimen_par(param_page_right_offset_code)
#define page_top_offset dimen_par(param_page_top_offset_code)
#define page_bottom_offset dimen_par(param_page_bottom_offset_code)
#define h_offset dimen_par(param_h_offset_code)
#define v_offset dimen_par(param_v_offset_code)

#define count(A) zeqtb[count_base+(A)].cint


/*
The most important output produced by a run of \TeX\ is the ``device
independent'' (\.{DVI}) file that specifies where characters and rules
are to appear on printed pages. The form of these files was designed by
David R. Fuchs in 1979. Almost any reasonable typesetting device can be
@^Fuchs, David Raymond@>
@:DVI_files}{\.{DVI} files@>
driven by a program that takes \.{DVI} files as input, and dozens of such
\.{DVI}-to-whatever programs have been written. Thus, it is possible to
print the output of \TeX\ on many different kinds of equipment, using \TeX\
as a device-independent ``front end.''

A \.{DVI} file is a stream of 8-bit bytes, which may be regarded as a
series of commands in a machine-like language. The first byte of each command
is the operation code, and this code is followed by zero or more bytes
that provide parameters to the command. The parameters themselves may consist
of several consecutive bytes; for example, the `|set_rule|' command has two
parameters, each of which is four bytes long. Parameters are usually
regarded as nonnegative integers; but four-byte-long parameters,
and shorter parameters that denote distances, can be
either positive or negative. Such parameters are given in two's complement
notation. For example, a two-byte-long distance parameter has a value between
$-2^{15}$ and $2^{15}-1$. As in \.{TFM} files, numbers that occupy
more than one byte position appear in BigEndian order.

A \.{DVI} file consists of a ``preamble,'' followed by a sequence of one
or more ``pages,'' followed by a ``postamble.'' The preamble is simply a
|pre| command, with its parameters that define the dimensions used in the
file; this must come first.  Each ``page'' consists of a |bop| command,
followed by any number of other commands that tell where characters are to
be placed on a physical page, followed by an |eop| command. The pages
appear in the order that \TeX\ generated them. If we ignore |nop| commands
and \\{fnt\_def} commands (which are allowed between any two commands in
the file), each |eop| command is immediately followed by a |bop| command,
or by a |post| command; in the latter case, there are no more pages in the
file, and the remaining bytes form the postamble.  Further details about
the postamble will be explained later.

Some parameters in \.{DVI} commands are ``pointers.'' These are four-byte
quantities that give the location number of some other byte in the file;
the first byte is number~0, then comes number~1, and so on. For example,
one of the parameters of a |bop| command points to the previous |bop|;
this makes it feasible to read the pages in backwards order, in case the
results are being directed to a device that stacks its output face up.
Suppose the preamble of a \.{DVI} file occupies bytes 0 to 99. Now if the
first page occupies bytes 100 to 999, say, and if the second
page occupies bytes 1000 to 1999, then the |bop| that starts in byte 1000
points to 100 and the |bop| that starts in byte 2000 points to 1000. (The
very first |bop|, i.e., the one starting in byte 100, has a pointer of~$-1$.)

@ The \.{DVI} format is intended to be both compact and easily interpreted
by a machine. Compactness is achieved by making most of the information
implicit instead of explicit. When a \.{DVI}-reading program reads the
commands for a page, it keeps track of several quantities: (a)~The current
font |f| is an integer; this value is changed only
by \\{fnt} and \\{fnt\_num} commands. (b)~The current position on the page
is given by two numbers called the horizontal and vertical coordinates,
|h| and |v|. Both coordinates are zero at the upper left corner of the page;
moving to the right corresponds to increasing the horizontal coordinate, and
moving down corresponds to increasing the vertical coordinate. Thus, the
coordinates are essentially Cartesian, except that vertical directions are
flipped; the Cartesian version of |(h,v)| would be |(h,-v)|.  (c)~The
current spacing amounts are given by four numbers |w|, |x|, |y|, and |z|,
where |w| and~|x| are used for horizontal spacing and where |y| and~|z|
are used for vertical spacing. (d)~There is a stack containing
|(h,v,w,x,y,z)| values; the \.{DVI} commands |push| and |pop| are used to
change the current level of operation. Note that the current font~|f| is
not pushed and popped; the stack contains only information about
positioning.

The values of |h|, |v|, |w|, |x|, |y|, and |z| are signed integers having up
to 32 bits, including the sign. Since they represent physical distances,
there is a small unit of measurement such that increasing |h| by~1 means
moving a certain tiny distance to the right. The actual unit of
measurement is variable, as explained below; \TeX\ sets things up so that
its \.{DVI} output is in sp units, i.e., scaled points, in agreement with
all the |scaled| dimensions in \TeX's data structures.

@ Here is a list of all the commands that may appear in a \.{DVI} file. Each
command is specified by its symbolic name (e.g., |bop|), its opcode byte
(e.g., 139), and its parameters (if any). The parameters are followed
by a bracketed number telling how many bytes they occupy; for example,
`|p[4]|' means that parameter |p| is four bytes long.

\yskip\hang|set_char_0| 0. Typeset character number~0 from font~|f|
such that the reference point of the character is at |(h,v)|. Then
increase |h| by the width of that character. Note that a character may
have zero or negative width, so one cannot be sure that |h| will advance
after this command; but |h| usually does increase.

\yskip\hang\\{set\_char\_1} through \\{set\_char\_127} (opcodes 1 to 127).
Do the operations of |set_char_0|; but use the character whose number
matches the opcode, instead of character~0.

\yskip\hang|set1| 128 |c[1]|. Same as |set_char_0|, except that character
number~|c| is typeset. \TeX82 uses this command for characters in the
range |128<=c<256|.

\yskip\hang|@!set2| 129 |c[2]|. Same as |set1|, except that |c|~is two
bytes long, so it is in the range |0<=c<65536|. \TeX82 never uses this
command, but it should come in handy for extensions of \TeX\ that deal
with oriental languages.
@^oriental characters@>@^Chinese characters@>@^Japanese characters@>

\yskip\hang|@!set3| 130 |c[3]|. Same as |set1|, except that |c|~is three
bytes long, so it can be as large as $2^{24}-1$. Not even the Chinese
language has this many characters, but this command might prove useful
in some yet unforeseen extension.

\yskip\hang|@!set4| 131 |c[4]|. Same as |set1|, except that |c|~is four
bytes long. Imagine that.

\yskip\hang|set_rule| 132 |a[4]| |b[4]|. Typeset a solid black rectangle
of height~|a| and width~|b|, with its bottom left corner at |(h,v)|. Then
set |h:=h+b|. If either |a<=0| or |b<=0|, nothing should be typeset. Note
that if |b<0|, the value of |h| will decrease even though nothing else happens.
See below for details about how to typeset rules so that consistency with
\MF\ is guaranteed.

\yskip\hang|@!put1| 133 |c[1]|. Typeset character number~|c| from font~|f|
such that the reference point of the character is at |(h,v)|. (The `put'
commands are exactly like the `set' commands, except that they simply put out a
character or a rule without moving the reference point afterwards.)

\yskip\hang|@!put2| 134 |c[2]|. Same as |set2|, except that |h| is not changed.

\yskip\hang|@!put3| 135 |c[3]|. Same as |set3|, except that |h| is not changed.

\yskip\hang|@!put4| 136 |c[4]|. Same as |set4|, except that |h| is not changed.

\yskip\hang|put_rule| 137 |a[4]| |b[4]|. Same as |set_rule|, except that
|h| is not changed.

\yskip\hang|nop| 138. No operation, do nothing. Any number of |nop|'s
may occur between \.{DVI} commands, but a |nop| cannot be inserted between
a command and its parameters or between two parameters.

\yskip\hang|bop| 139 $c_0[4]$ $c_1[4]$ $\ldots$ $c_9[4]$ $p[4]$. Beginning
of a page: Set |(h,v,w,x,y,z):=(0,0,0,0,0,0)| and set the stack empty. Set
the current font |f| to an undefined value.  The ten $c_i$ parameters hold
the values of \.{\\count0} $\ldots$ \.{\\count9} in \TeX\ at the time
\.{\\shipout} was invoked for this page; they can be used to identify
pages, if a user wants to print only part of a \.{DVI} file. The parameter
|p| points to the previous |bop| in the file; the first
|bop| has $p=-1$.

\yskip\hang|eop| 140.  End of page: Print what you have read since the
previous |bop|. At this point the stack should be empty. (The \.{DVI}-reading
programs that drive most output devices will have kept a buffer of the
material that appears on the page that has just ended. This material is
largely, but not entirely, in order by |v| coordinate and (for fixed |v|) by
|h|~coordinate; so it usually needs to be sorted into some order that is
appropriate for the device in question.)

\yskip\hang|push| 141. Push the current values of |(h,v,w,x,y,z)| onto the
top of the stack; do not change any of these values. Note that |f| is
not pushed.

\yskip\hang|pop| 142. Pop the top six values off of the stack and assign
them respectively to |(h,v,w,x,y,z)|. The number of pops should never
exceed the number of pushes, since it would be highly embarrassing if the
stack were empty at the time of a |pop| command.

\yskip\hang|right1| 143 |b[1]|. Set |h:=h+b|, i.e., move right |b| units.
The parameter is a signed number in two's complement notation, |-128<=b<128|;
if |b<0|, the reference point moves left.

\yskip\hang|right2| 144 |b[2]|. Same as |right1|, except that |b| is a
two-byte quantity in the range |-32768<=b<32768|.

\yskip\hang|right3| 145 |b[3]|. Same as |right1|, except that |b| is a
three-byte quantity in the range |@t$-2^{23}$@><=b<@t$2^{23}$@>|.

\yskip\hang|right4| 146 |b[4]|. Same as |right1|, except that |b| is a
four-byte quantity in the range |@t$-2^{31}$@><=b<@t$2^{31}$@>|.

\yskip\hang|w0| 147. Set |h:=h+w|; i.e., move right |w| units. With luck,
this parameterless command will usually suffice, because the same kind of motion
will occur several times in succession; the following commands explain how
|w| gets particular values.

\yskip\hang|w1| 148 |b[1]|. Set |w:=b| and |h:=h+b|. The value of |b| is a
signed quantity in two's complement notation, |-128<=b<128|. This command
changes the current |w|~spacing and moves right by |b|.

\yskip\hang|@!w2| 149 |b[2]|. Same as |w1|, but |b| is two bytes long,
|-32768<=b<32768|.

\yskip\hang|@!w3| 150 |b[3]|. Same as |w1|, but |b| is three bytes long,
|@t$-2^{23}$@><=b<@t$2^{23}$@>|.

\yskip\hang|@!w4| 151 |b[4]|. Same as |w1|, but |b| is four bytes long,
|@t$-2^{31}$@><=b<@t$2^{31}$@>|.

\yskip\hang|x0| 152. Set |h:=h+x|; i.e., move right |x| units. The `|x|'
commands are like the `|w|' commands except that they involve |x| instead
of |w|.

\yskip\hang|x1| 153 |b[1]|. Set |x:=b| and |h:=h+b|. The value of |b| is a
signed quantity in two's complement notation, |-128<=b<128|. This command
changes the current |x|~spacing and moves right by |b|.

\yskip\hang|@!x2| 154 |b[2]|. Same as |x1|, but |b| is two bytes long,
|-32768<=b<32768|.

\yskip\hang|@!x3| 155 |b[3]|. Same as |x1|, but |b| is three bytes long,
|@t$-2^{23}$@><=b<@t$2^{23}$@>|.

\yskip\hang|@!x4| 156 |b[4]|. Same as |x1|, but |b| is four bytes long,
|@t$-2^{31}$@><=b<@t$2^{31}$@>|.

\yskip\hang|down1| 157 |a[1]|. Set |v:=v+a|, i.e., move down |a| units.
The parameter is a signed number in two's complement notation, |-128<=a<128|;
if |a<0|, the reference point moves up.

\yskip\hang|@!down2| 158 |a[2]|. Same as |down1|, except that |a| is a
two-byte quantity in the range |-32768<=a<32768|.

\yskip\hang|@!down3| 159 |a[3]|. Same as |down1|, except that |a| is a
three-byte quantity in the range |@t$-2^{23}$@><=a<@t$2^{23}$@>|.

\yskip\hang|@!down4| 160 |a[4]|. Same as |down1|, except that |a| is a
four-byte quantity in the range |@t$-2^{31}$@><=a<@t$2^{31}$@>|.

\yskip\hang|y0| 161. Set |v:=v+y|; i.e., move down |y| units. With luck,
this parameterless command will usually suffice, because the same kind of motion
will occur several times in succession; the following commands explain how
|y| gets particular values.

\yskip\hang|y1| 162 |a[1]|. Set |y:=a| and |v:=v+a|. The value of |a| is a
signed quantity in two's complement notation, |-128<=a<128|. This command
changes the current |y|~spacing and moves down by |a|.

\yskip\hang|@!y2| 163 |a[2]|. Same as |y1|, but |a| is two bytes long,
|-32768<=a<32768|.

\yskip\hang|@!y3| 164 |a[3]|. Same as |y1|, but |a| is three bytes long,
|@t$-2^{23}$@><=a<@t$2^{23}$@>|.

\yskip\hang|@!y4| 165 |a[4]|. Same as |y1|, but |a| is four bytes long,
|@t$-2^{31}$@><=a<@t$2^{31}$@>|.

\yskip\hang|z0| 166. Set |v:=v+z|; i.e., move down |z| units. The `|z|' commands
are like the `|y|' commands except that they involve |z| instead of |y|.

\yskip\hang|z1| 167 |a[1]|. Set |z:=a| and |v:=v+a|. The value of |a| is a
signed quantity in two's complement notation, |-128<=a<128|. This command
changes the current |z|~spacing and moves down by |a|.

\yskip\hang|@!z2| 168 |a[2]|. Same as |z1|, but |a| is two bytes long,
|-32768<=a<32768|.

\yskip\hang|@!z3| 169 |a[3]|. Same as |z1|, but |a| is three bytes long,
|@t$-2^{23}$@><=a<@t$2^{23}$@>|.

\yskip\hang|@!z4| 170 |a[4]|. Same as |z1|, but |a| is four bytes long,
|@t$-2^{31}$@><=a<@t$2^{31}$@>|.

\yskip\hang|fnt_num_0| 171. Set |f:=0|. Font 0 must previously have been
defined by a \\{fnt\_def} instruction, as explained below.

\yskip\hang\\{fnt\_num\_1} through \\{fnt\_num\_63} (opcodes 172 to 234). Set
|f:=1|, \dots, \hbox{|f:=63|}, respectively.

\yskip\hang|fnt1| 235 |k[1]|. Set |f:=k|. \TeX82 uses this command for font
numbers in the range |64<=k<256|.

\yskip\hang|@!fnt2| 236 |k[2]|. Same as |fnt1|, except that |k|~is two
bytes long, so it is in the range |0<=k<65536|. \TeX82 never generates this
command, but large font numbers may prove useful for specifications of
color or texture, or they may be used for special fonts that have fixed
numbers in some external coding scheme.

\yskip\hang|@!fnt3| 237 |k[3]|. Same as |fnt1|, except that |k|~is three
bytes long, so it can be as large as $2^{24}-1$.

\yskip\hang|@!fnt4| 238 |k[4]|. Same as |fnt1|, except that |k|~is four
bytes long; this is for the really big font numbers (and for the negative ones).

\yskip\hang|xxx1| 239 |k[1]| |x[k]|. This command is undefined in
general; it functions as a $(k+2)$-byte |nop| unless special \.{DVI}-reading
programs are being used. \TeX82 generates |xxx1| when a short enough
\.{\\special} appears, setting |k| to the number of bytes being sent. It
is recommended that |x| be a string having the form of a keyword followed
by possible parameters relevant to that keyword.

\yskip\hang|@!xxx2| 240 |k[2]| |x[k]|. Like |xxx1|, but |0<=k<65536|.

\yskip\hang|@!xxx3| 241 |k[3]| |x[k]|. Like |xxx1|, but |0<=k<@t$2^{24}$@>|.

\yskip\hang|xxx4| 242 |k[4]| |x[k]|. Like |xxx1|, but |k| can be ridiculously
large. \TeX82 uses |xxx4| when sending a string of length 256 or more.

\yskip\hang|fnt_def1| 243 |k[1]| |c[4]| |s[4]| |d[4]| |a[1]| |l[1]| |n[a+l]|.
Define font |k|, where |0<=k<256|; font definitions will be explained shortly.

\yskip\hang|@!fnt_def2| 244 |k[2]| |c[4]| |s[4]| |d[4]| |a[1]| |l[1]| |n[a+l]|.
Define font |k|, where |0<=k<65536|.

\yskip\hang|@!fnt_def3| 245 |k[3]| |c[4]| |s[4]| |d[4]| |a[1]| |l[1]| |n[a+l]|.
Define font |k|, where |0<=k<@t$2^{24}$@>|.

\yskip\hang|@!fnt_def4| 246 |k[4]| |c[4]| |s[4]| |d[4]| |a[1]| |l[1]| |n[a+l]|.
Define font |k|, where |@t$-2^{31}$@><=k<@t$2^{31}$@>|.

\yskip\hang|pre| 247 |i[1]| |num[4]| |den[4]| |mag[4]| |k[1]| |x[k]|.
Beginning of the preamble; this must come at the very beginning of the
file. Parameters |i|, |num|, |den|, |mag|, |k|, and |x| are explained below.

\yskip\hang|post| 248. Beginning of the postamble, see below.

\yskip\hang|post_post| 249. Ending of the postamble, see below.

\yskip\noindent Commands 250--255 are undefined at the present time.
*/

#define set_char_0  0           /* typeset character 0 and move right */
#define set1  128               /* typeset a character and move right */
#define set_rule  132           /* typeset a rule and move right */
#define put1    133             /* typeset a character without moving */
#define put_rule  137           /* typeset a rule */
#define nop  138                /* no operation */
#define bop  139                /* beginning of page */
#define eop  140                /* ending of page */
#define push  141               /* save the current positions */
#define pop  142                /* restore previous positions */
#define right1    143           /* move right */
#define right4    146           /* move right, 4 bytes */
#define w0  147                 /* move right by |w| */
#define w1  148                 /* move right and set |w| */
#define x0  152                 /* move right by |x| */
#define x1  153                 /* move right and set |x| */
#define down1  157              /* move down */
#define down4  160              /* move down, 4 bytes */
#define y0  161                 /* move down by |y| */
#define y1  162                 /* move down and set |y| */
#define z0  166                 /* move down by |z| */
#define z1  167                 /* move down and set |z| */
#define fnt_num_0  171          /* set current font to 0 */
#define fnt1  235               /* set current font */
#define xxx1  239               /* extension to \.{DVI} primitives */
#define xxx4  242               /* potentially long extension to \.{DVI} primitives */
#define fnt_def1  243           /* define the meaning of a font number */
#define pre  247                /* preamble */
#define post  248               /* postamble beginning */
#define post_post  249          /* postamble ending */

/*
The preamble contains basic information about the file as a whole. As
stated above, there are six parameters:
$$\hbox{|@!i[1]| |@!num[4]| |@!den[4]| |@!mag[4]| |@!k[1]| |@!x[k]|.}$$
The |i| byte identifies \.{DVI} format; currently this byte is always set
to~2. (The value |i=3| is currently used for an extended format that
allows a mixture of right-to-left and left-to-right typesetting.
Some day we will set |i=4|, when \.{DVI} format makes another
incompatible change---perhaps in the year 2048.)

The next two parameters, |num| and |den|, are positive integers that define
the units of measurement; they are the numerator and denominator of a
fraction by which all dimensions in the \.{DVI} file could be multiplied
in order to get lengths in units of $10^{-7}$ meters. Since $\rm 7227{pt} =
254{cm}$, and since \TeX\ works with scaled points where there are $2^{16}$
sp in a point, \TeX\ sets
$|num|/|den|=(254\cdot10^5)/(7227\cdot2^{16})=25400000/473628672$.
@^sp@>

The |mag| parameter is what \TeX\ calls \.{\\mag}, i.e., 1000 times the
desired magnification. The actual fraction by which dimensions are
multiplied is therefore $|mag|\cdot|num|/1000|den|$. Note that if a \TeX\
source document does not call for any `\.{true}' dimensions, and if you
change it only by specifying a different \.{\\mag} setting, the \.{DVI}
file that \TeX\ creates will be completely unchanged except for the value
of |mag| in the preamble and postamble. (Fancy \.{DVI}-reading programs allow
users to override the |mag|~setting when a \.{DVI} file is being printed.)

Finally, |k| and |x| allow the \.{DVI} writer to include a comment, which is not
interpreted further. The length of comment |x| is |k|, where |0<=k<256|.
*/

#define id_byte 2               /* identifies the kind of \.{DVI} files described here */

/*
Font definitions for a given font number |k| contain further parameters
$$\hbox{|c[4]| |s[4]| |d[4]| |a[1]| |l[1]| |n[a+l]|.}$$
The four-byte value |c| is the check sum that \TeX\ found in the \.{TFM}
file for this font; |c| should match the check sum of the font found by
programs that read this \.{DVI} file.
@^check sum@>

Parameter |s| contains a fixed-point scale factor that is applied to
the character widths in font |k|; font dimensions in \.{TFM} files and
other font files are relative to this quantity, which is called the
``at size'' elsewhere in this documentation. The value of |s| is
always positive and less than $2^{27}$. It is given in the same units
as the other \.{DVI} dimensions, i.e., in sp when \TeX82 has made the
file.  Parameter |d| is similar to |s|; it is the ``design size,'' and
(like~|s|) it is given in \.{DVI} units. Thus, font |k| is to be used
at $|mag|\cdot s/1000d$ times its normal size.

The remaining part of a font definition gives the external name of the font,
which is an ASCII string of length |a+l|. The number |a| is the length
of the ``area'' or directory, and |l| is the length of the font name itself;
the standard local system font area is supposed to be used when |a=0|.
The |n| field contains the area in its first |a| bytes.

Font definitions must appear before the first use of a particular font number.
Once font |k| is defined, it must not be defined again; however, we
shall see below that font definitions appear in the postamble as well as
in the pages, so in this sense each font number is defined exactly twice,
if at all. Like |nop| commands, font definitions can
appear before the first |bop|, or between an |eop| and a |bop|.

@ Sometimes it is desirable to make horizontal or vertical rules line up
precisely with certain features in characters of a font. It is possible to
guarantee the correct matching between \.{DVI} output and the characters
generated by \MF\ by adhering to the following principles: (1)~The \MF\
characters should be positioned so that a bottom edge or left edge that is
supposed to line up with the bottom or left edge of a rule appears at the
reference point, i.e., in row~0 and column~0 of the \MF\ raster. This
ensures that the position of the rule will not be rounded differently when
the pixel size is not a perfect multiple of the units of measurement in
the \.{DVI} file. (2)~A typeset rule of height $a>0$ and width $b>0$
should be equivalent to a \MF-generated character having black pixels in
precisely those raster positions whose \MF\ coordinates satisfy
|0<=x<@t$\alpha$@>b| and |0<=y<@t$\alpha$@>a|, where $\alpha$ is the number
of pixels per \.{DVI} unit.
@:METAFONT}{\MF@>
@^alignment of rules with characters@>
@^rules aligning with characters@>

@ The last page in a \.{DVI} file is followed by `|post|'; this command
introduces the postamble, which summarizes important facts that \TeX\ has
accumulated about the file, making it possible to print subsets of the data
with reasonable efficiency. The postamble has the form
$$\vbox{\halign{\hbox{#\hfil}\cr
  |post| |p[4]| |num[4]| |den[4]| |mag[4]| |l[4]| |u[4]| |s[2]| |t[2]|\cr
  $\langle\,$font definitions$\,\rangle$\cr
  |post_post| |q[4]| |i[1]| 223's$[{\G}4]$\cr}}$$
Here |p| is a pointer to the final |bop| in the file. The next three
parameters, |num|, |den|, and |mag|, are duplicates of the quantities that
appeared in the preamble.

Parameters |l| and |u| give respectively the height-plus-depth of the tallest
page and the width of the widest page, in the same units as other dimensions
of the file. These numbers might be used by a \.{DVI}-reading program to
position individual ``pages'' on large sheets of film or paper; however,
the standard convention for output on normal size paper is to position each
page so that the upper left-hand corner is exactly one inch from the left
and the top. Experience has shown that it is unwise to design \.{DVI}-to-printer
software that attempts cleverly to center the output; a fixed position of
the upper left corner is easiest for users to understand and to work with.
Therefore |l| and~|u| are often ignored.

Parameter |s| is the maximum stack depth (i.e., the largest excess of
|push| commands over |pop| commands) needed to process this file. Then
comes |t|, the total number of pages (|bop| commands) present.

The postamble continues with font definitions, which are any number of
\\{fnt\_def} commands as described above, possibly interspersed with |nop|
commands. Each font number that is used in the \.{DVI} file must be defined
exactly twice: Once before it is first selected by a \\{fnt} command, and once
in the postamble.

@ The last part of the postamble, following the |post_post| byte that
signifies the end of the font definitions, contains |q|, a pointer to the
|post| command that started the postamble.  An identification byte, |i|,
comes next; this currently equals~2, as in the preamble.

The |i| byte is followed by four or more bytes that are all equal to
the decimal number 223 (i.e., @'337 in octal). \TeX\ puts out four to seven of
these trailing bytes, until the total length of the file is a multiple of
four bytes, since this works out best on machines that pack four bytes per
word; but any number of 223's is allowed, as long as there are at least four
of them. In effect, 223 is a sort of signature that is added at the very end.
@^Fuchs, David Raymond@>

This curious way to finish off a \.{DVI} file makes it feasible for
\.{DVI}-reading programs to find the postamble first, on most computers,
even though \TeX\ wants to write the postamble last. Most operating
systems permit random access to individual words or bytes of a file, so
the \.{DVI} reader can start at the end and skip backwards over the 223's
until finding the identification byte. Then it can back up four bytes, read
|q|, and move to byte |q| of the file. This byte should, of course,
contain the value 248 (|post|); now the postamble can be read, so the
\.{DVI} reader can discover all the information needed for typesetting the
pages. Note that it is also possible to skip through the \.{DVI} file at
reasonably high speed to locate a particular page, if that proves
desirable. This saves a lot of time, since \.{DVI} files used in production
jobs tend to be large.

Unfortunately, however, standard \PASCAL\ does not include the ability to
@^system dependencies@>
access a random position in a file, or even to determine the length of a file.
Almost all systems nowadays provide the necessary capabilities, so \.{DVI}
format has been designed to work most efficiently with modern operating systems.
But if \.{DVI} files have to be processed under the restrictions of standard
\PASCAL, one can simply read them from front to back, since the necessary
header information is present in the preamble and in the font definitions.
(The |l| and |u| and |s| and |t| parameters, which appear only in the
postamble, are ``frills'' that are handy but not absolutely necessary.)
*/

/*
@* \[32] Shipping pages out.
After considering \TeX's eyes and stomach, we come now to the bowels.
@^bowels@>

The |ship_out| procedure is given a pointer to a box; its mission is
to describe that box in \.{DVI} form, outputting a ``page'' to |dvi_file|.
The \.{DVI} coordinates $(h,v)=(0,0)$ should correspond to the upper left
corner of the box being shipped.

Since boxes can be inside of boxes inside of boxes, the main work of
|ship_out| is done by two mutually recursive routines, |hlist_out|
and |vlist_out|, which traverse the hlists and vlists inside of horizontal
and vertical boxes.

As individual pages are being processed, we need to accumulate
information about the entire set of pages, since such statistics must be
reported in the postamble. The global variables |total_pages|, |max_v|,
|max_h|, |max_push|, and |last_bop| are used to record this information.

The variable |doing_leaders| is |true| while leaders are being output.
The variable |dead_cycles| contains the number of times an output routine
has been initiated since the last |ship_out|.

A few additional global variables are also defined here for use in
|vlist_out| and |hlist_out|. They could have been local variables, but
that would waste stack space when boxes are deeply nested, since the
values of these variables are not needed during recursive calls.
@^recursion@>
*/

integer total_pages = 0;        /* the number of pages that have been shipped out */
scaled max_v = 0;               /* maximum height-plus-depth of pages shipped so far */
scaled max_h = 0;               /* maximum width of pages shipped so far */
integer max_push = 0;           /* deepest nesting of |push| commands encountered so far */
integer last_bop = -1;          /* location of previous |bop| in the \.{DVI} output */
integer dead_cycles = 0;        /* recent outputs that didn't ship anything out */
boolean doing_leaders = false;  /* are we inside a leader box? */
integer c, f;                   /* character and font in current |char_node| */
integer oval, ocmd;             /* used by |out_cmd| for generating |set|, |fnt| and |fnt_def| commands */
scaled rule_ht, rule_dp, rule_wd;       /* size of current rule being output */
pointer g;                      /* current glue specification */
integer lq, lr;                 /* quantities used in calculations for leaders */
integer cur_s = -1;             /* current depth of output box nesting, initially $-1$ */

/*
@ The \.{DVI} bytes are output to a buffer instead of being written directly
to the output file. This makes it possible to reduce the overhead of
subroutine calls, thereby measurably speeding up the computation, since
output of \.{DVI} bytes is part of \TeX's inner loop. And it has another
advantage as well, since we can change instructions in the buffer in order to
make the output more compact. For example, a `|down2|' command can be
changed to a `|y2|', thereby making a subsequent `|y0|' command possible,
saving two bytes.

The output buffer is divided into two parts of equal size; the bytes found
in |dvi_buf[0..half_buf-1]| constitute the first half, and those in
|dvi_buf[half_buf..dvi_buf_size-1]| constitute the second. The global
variable |dvi_ptr| points to the position that will receive the next
output byte. When |dvi_ptr| reaches |dvi_limit|, which is always equal
to one of the two values |half_buf| or |dvi_buf_size|, the half buffer that
is about to be invaded next is sent to the output and |dvi_limit| is
changed to its other value. Thus, there is always at least a half buffer's
worth of information present, except at the very beginning of the job.

Bytes of the \.{DVI} file are numbered sequentially starting with 0;
the next byte to be generated will be number |dvi_offset+dvi_ptr|.
A byte is present in the buffer only if its number is |>=dvi_gone|.
*/

/*
Some systems may find it more efficient to make |dvi_buf| a |packed|
array, since output of four bytes at once may be facilitated.
@^system dependencies@>
*/

/*
Initially the buffer is all in one piece; we will output half of it only
after it first fills up.
*/

integer dvi_buf_size = 800;     /* size of the output buffer; must be a multiple of 8 */
real_eight_bits *dvi_buf;       /* buffer for \.{DVI} output */
dvi_index half_buf = 0;         /* half of |dvi_buf_size| */
dvi_index dvi_limit = 0;        /* end of the current half buffer */
dvi_index dvi_ptr = 0;          /* the next available buffer address */
integer dvi_offset = 0;         /* |dvi_buf_size| times the number of times the output buffer has been fully emptied */
integer dvi_gone = 0;           /* the number of bytes already output to |dvi_file| */

/*
The actual output of |dvi_buf[a..b]| to |dvi_file| is performed by calling
|write_dvi(a,b)|. For best results, this procedure should be optimized to
run as fast as possible on each particular system, since it is part of
\TeX's inner loop. It is safe to assume that |a| and |b+1| will both be
multiples of 4 when |write_dvi(a,b)| is called; therefore it is possible on
many machines to use efficient methods to pack four bytes per word and to
output an array of words with one system call.
@^system dependencies@>
@^inner loop@>
@^defecation@>
*/

void write_dvi(dvi_index a, dvi_index b)
{
    dvi_index k;
    for (k = a; k <= b; k++)
        fputc(dvi_buf[k], dvi_file);
}

/* outputs half of the buffer */
void dvi_swap(void)
{
    if (dvi_limit == dvi_buf_size) {
        write_dvi(0, half_buf - 1);
        dvi_limit = half_buf;
        dvi_offset = dvi_offset + dvi_buf_size;
        dvi_ptr = 0;
    } else {
        write_dvi(half_buf, dvi_buf_size - 1);
        dvi_limit = dvi_buf_size;
    }
    dvi_gone = dvi_gone + half_buf;
}

/*
The |dvi_four| procedure outputs four bytes in two's complement notation,
without risking arithmetic overflow.
*/

void dvi_four(integer x)
{
    if (x >= 0) {
        dvi_out(x / 0100000000);
    } else {
        x = x + 010000000000;
        x = x + 010000000000;
        dvi_out((x / 0100000000) + 128);
    }
    x = x % 0100000000;
    dvi_out(x / 0200000);
    x = x % 0200000;
    dvi_out(x / 0400);
    dvi_out(x % 0400);
}

/*
A mild optimization of the output is performed by the |dvi_pop|
routine, which issues a |pop| unless it is possible to cancel a
`|push| |pop|' pair. The parameter to |dvi_pop| is the byte address
following the old |push| that matches the new |pop|.
*/

void dvi_pop(integer l)
{
    if ((l == dvi_offset + dvi_ptr) && (dvi_ptr > 0))
        decr(dvi_ptr);
    else
        dvi_out(pop);
}

/*
Here's a procedure that outputs a font definition. $\Omega$ allows
more than 256 different fonts per job, so the right font definition
command must be selected.
*/

void out_cmd(void)
{
    if ((oval < 0x100) && (oval >= 0)) {
        if ((ocmd != set1) || (oval > 127)) {
            if ((ocmd == fnt1) && (oval < 64))
                oval += fnt_num_0;
            else
                dvi_out(ocmd);
        }
    } else {
        if ((oval < 0x10000) && (oval >= 0)) {
            dvi_out(ocmd + 1);
        } else {
            if ((oval < 0x1000000) && (oval >= 0)) {
                dvi_out(ocmd + 2);
            } else {
                dvi_out(ocmd + 3);
                if (oval >= 0) {
                    dvi_out(oval / 0x1000000);
                } else {
                    oval += 0x40000000;
                    oval += 0x40000000;
                    dvi_out((oval / 0x1000000) + 128);
                    oval = oval % 0x1000000;
                }
                dvi_out(oval / 0x10000);
                oval = oval % 0x10000;
            }
            dvi_out(oval / 0x10000);
            oval = oval % 0x10000;
        }
        dvi_out(oval / 0x100);
        oval = oval % 0x100;
    }
    dvi_out(oval);
}

void dvi_font_def(internal_font_number f)
{
    char *fa;
    oval = f - 1;
    ocmd = fnt_def1;
    out_cmd();
    dvi_out(font_check_0(f));
    dvi_out(font_check_1(f));
    dvi_out(font_check_2(f));
    dvi_out(font_check_3(f));
    dvi_four(font_size(f));
    dvi_four(font_dsize(f));
    dvi_out(0);                 /* |font_area(f)| is unused */
    dvi_out(strlen(font_name(f)));
    /* Output the font name whose internal number is |f| */
    fa = font_name(f);
    while (*fa != '\0') {
        dvi_out(*fa++);
    }
}

/*

@ Versions of \TeX\ intended for small computers might well choose to omit
the ideas in the next few parts of this program, since it is not really
necessary to optimize the \.{DVI} code by making use of the |w0|, |x0|,
|y0|, and |z0| commands. Furthermore, the algorithm that we are about to
describe does not pretend to give an optimum reduction in the length
of the \.{DVI} code; after all, speed is more important than compactness.
But the method is surprisingly effective, and it takes comparatively little
time.

We can best understand the basic idea by first considering a simpler problem
that has the same essential characteristics. Given a sequence of digits,
say $3\,1\,4\,1\,5\,9\,2\,6\,5\,3\,5\,8\,9$, we want to assign subscripts
$d$, $y$, or $z$ to each digit so as to maximize the number of ``$y$-hits''
and ``$z$-hits''; a $y$-hit is an instance of two appearances of the same
digit with the subscript $y$, where no $y$'s intervene between the two
appearances, and a $z$-hit is defined similarly. For example, the sequence
above could be decorated with subscripts as follows:
$$3_z\,1_y\,4_d\,1_y\,5_y\,9_d\,2_d\,6_d\,5_y\,3_z\,5_y\,8_d\,9_d.$$
There are three $y$-hits ($1_y\ldots1_y$ and $5_y\ldots5_y\ldots5_y$) and
one $z$-hit ($3_z\ldots3_z$); there are no $d$-hits, since the two appearances
of $9_d$ have $d$'s between them, but we don't count $d$-hits so it doesn't
matter how many there are. These subscripts are analogous to the \.{DVI}
commands called \\{down}, $y$, and $z$, and the digits are analogous to
different amounts of vertical motion; a $y$-hit or $z$-hit corresponds to
the opportunity to use the one-byte commands |y0| or |z0| in a \.{DVI} file.

\TeX's method of assigning subscripts works like this: Append a new digit,
say $\delta$, to the right of the sequence. Now look back through the
sequence until one of the following things happens: (a)~You see
$\delta_y$ or $\delta_z$, and this was the first time you encountered a
$y$ or $z$ subscript, respectively.  Then assign $y$ or $z$ to the new
$\delta$; you have scored a hit. (b)~You see $\delta_d$, and no $y$
subscripts have been encountered so far during this search.  Then change
the previous $\delta_d$ to $\delta_y$ (this corresponds to changing a
command in the output buffer), and assign $y$ to the new $\delta$; it's
another hit.  (c)~You see $\delta_d$, and a $y$ subscript has been seen
but not a $z$.  Change the previous $\delta_d$ to $\delta_z$ and assign
$z$ to the new $\delta$. (d)~You encounter both $y$ and $z$ subscripts
before encountering a suitable $\delta$, or you scan all the way to the
front of the sequence. Assign $d$ to the new $\delta$; this assignment may
be changed later.

The subscripts $3_z\,1_y\,4_d\ldots\,$ in the example above were, in fact,
produced by this procedure, as the reader can verify. (Go ahead and try it.)

*/

/*
@ In order to implement such an idea, \TeX\ maintains a stack of pointers
to the \\{down}, $y$, and $z$ commands that have been generated for the
current page. And there is a similar stack for \\{right}, |w|, and |x|
commands. These stacks are called the down stack and right stack, and their
top elements are maintained in the variables |down_ptr| and |right_ptr|.

Each entry in these stacks contains four fields: The |width| field is
the amount of motion down or to the right; the |location| field is the
byte number of the \.{DVI} command in question (including the appropriate
|dvi_offset|); the |vlink| field points to the next item below this one
on the stack; and the |vinfo| field encodes the options for possible change
in the \.{DVI} command.
*/

#define location(A) varmem[(A)+1].cint  /* \.{DVI} byte number for a movement command */

halfword down_ptr = null, right_ptr = null;     /* heads of the down and right stacks */

/*
Here is a subroutine that produces a \.{DVI} command for some specified
downward or rightward motion. It has two parameters: |w| is the amount
of motion, and |o| is either |down1| or |right1|. We use the fact that
the command codes have convenient arithmetic properties: |y1-down1=w1-right1|
and |z1-down1=x1-right1|.
*/

void movement(scaled w, eight_bits o)
{
    small_number mstate;        /* have we seen a |y| or |z|? */
    halfword p, q;              /* current and top nodes on the stack */
    integer k;                  /* index into |dvi_buf|, modulo |dvi_buf_size| */
    if (false) {                /* TODO: HUH? */
        q = new_node(movement_node, 0); /* new node for the top of the stack */
        width(q) = w;
        location(q) = dvi_offset + dvi_ptr;
        if (o == down1) {
            vlink(q) = down_ptr;
            down_ptr = q;
        } else {
            vlink(q) = right_ptr;
            right_ptr = q;
        }
        /* Look at the other stack entries until deciding what sort of \.{DVI} command
           to generate; |goto found| if node |p| is a ``hit'' */
        p = vlink(q);
        mstate = none_seen;
        while (p != null) {
            if (width(p) == w) {
                /* Consider a node with matching width;|goto found| if it's a hit */
                /* We might find a valid hit in a |y| or |z| byte that is already gone
                   from the buffer. But we can't change bytes that are gone forever; ``the
                   moving finger writes, $\ldots\,\,$.'' */

                switch (mstate + vinfo(p)) {
                case none_seen + yz_OK:
                case none_seen + y_OK:
                case z_seen + yz_OK:
                case z_seen + y_OK:
                    if (location(p) < dvi_gone) {
                        goto NOT_FOUND;
                    } else {
                        /* Change buffered instruction to |y| or |w| and |goto found| */
                        k = location(p) - dvi_offset;
                        if (k < 0)
                            k = k + dvi_buf_size;
                        dvi_buf[k] = dvi_buf[k] + y1 - down1;
                        vinfo(p) = y_here;
                        goto FOUND;
                    }
                    break;
                case none_seen + z_OK:
                case y_seen + yz_OK:
                case y_seen + z_OK:
                    if (location(p) < dvi_gone) {
                        goto NOT_FOUND;
                    } else {
                        /* Change buffered instruction to |z| or |x| and |goto found| */
                        k = location(p) - dvi_offset;
                        if (k < 0)
                            k = k + dvi_buf_size;
                        dvi_buf[k] = dvi_buf[k] + z1 - down1;
                        vinfo(p) = z_here;
                        goto FOUND;
                    }
                    break;
                case none_seen + y_here:
                case none_seen + z_here:
                case y_seen + z_here:
                case z_seen + y_here:
                    goto FOUND;
                    break;
                default:
                    break;
                }
            } else {
                switch (mstate + vinfo(p)) {
                case none_seen + y_here:
                    mstate = y_seen;
                    break;
                case none_seen + z_here:
                    mstate = z_seen;
                    break;
                case y_seen + z_here:
                case z_seen + y_here:
                    goto NOT_FOUND;
                    break;
                default:
                    break;
                }
            }
            p = vlink(p);
        }
    }
  NOT_FOUND:
    /* Generate a |down| or |right| command for |w| and |return| */
    if (abs(w) >= 040000000) {
        dvi_out(o + 3);         /* |down4| or |right4| */
        dvi_four(w);
        return;
    }
    if (abs(w) >= 0100000) {
        dvi_out(o + 2);         /* |down3| or |right3| */
        if (w < 0)
            w = w + 0100000000;
        dvi_out(w / 0200000);
        w = w % 0200000;
        goto TWO;
    }
    if (abs(w) >= 0200) {
        dvi_out(o + 1);         /* |down2| or |right2| */
        if (w < 0)
            w = w + 0200000;
        goto TWO;
    }
    dvi_out(o);                 /* |down1| or |right1| */
    if (w < 0)
        w = w + 0400;
    goto ONE;
  TWO:
    dvi_out(w / 0400);
  ONE:
    dvi_out(w % 0400);
    return;
  FOUND:
    /* Generate a |y0| or |z0| command in order to reuse a previous appearance of~|w| */
    /* The program below removes movement nodes that are introduced after a |push|,
       before it outputs the corresponding |pop|. */
    /*
       When the |movement| procedure gets to the label |found|, the value of
       |vinfo(p)| will be either |y_here| or |z_here|. If it is, say, |y_here|,
       the procedure generates a |y0| command (or a |w0| command), and marks
       all |vinfo| fields between |q| and |p| so that |y| is not OK in that range.
     */
    vinfo(q) = vinfo(p);
    if (vinfo(q) == y_here) {
        dvi_out(o + y0 - down1);        /* |y0| or |w0| */
        while (vlink(q) != p) {
            q = vlink(q);
            switch (vinfo(q)) {
            case yz_OK:
                vinfo(q) = z_OK;
                break;
            case y_OK:
                vinfo(q) = d_fixed;
                break;
            default:
                break;
            }
        }
    } else {
        dvi_out(o + z0 - down1);        /* |z0| or |x0| */
        while (vlink(q) != p) {
            q = vlink(q);
            switch (vinfo(q)) {
            case yz_OK:
                vinfo(q) = y_OK;
                break;
            case z_OK:
                vinfo(q) = d_fixed;
                break;
            default:
                break;
            }
        }
    }
}

/*
In case you are wondering when all the movement nodes are removed from
\TeX's memory, the answer is that they are recycled just before
|hlist_out| and |vlist_out| finish outputting a box. This restores the
down and right stacks to the state they were in before the box was output,
except that some |vinfo|'s may have become more restrictive.
*/

/* delete movement nodes with |location>=l| */
void prune_movements(integer l)
{
    pointer p;                  /* node being deleted */
    while (down_ptr != null) {
        if (location(down_ptr) < l)
            break;
        p = down_ptr;
        down_ptr = vlink(p);
        flush_node(p);
    }
    while (right_ptr != null) {
        if (location(right_ptr) < l)
            return;
        p = right_ptr;
        right_ptr = vlink(p);
        flush_node(p);
    }
}

scaledpos synch_p_with_c(scaledpos cur)
{
    scaledpos pos;
    pos.h = 0;                  /* FIXME: init is only done to silence a compiler warning */
    pos.v = 0;                  /* FIXME: init is only done to silence a compiler warning */
    synch_pos_with_cur();
    return pos;
}

scaledpos cur;                  /* \TeX\ position relative to origin of the surrounding box, in box coordinate system */
scaledpos box_pos;              /* position of box origin in page coordinates */
scaledpos pos;                  /* global position on page, in $\rm sp$, from lower left page corner */
scaledpos dvi;                  /* a \.{DVI} position in page coordinates, in sync with DVI file */
internal_font_number dvi_f;     /* the current font */

scaledpos cur_page_size;

integer get_cur_v(void)
{
    return (cur_page_size.v - cur.v);
}

integer get_cur_h(void)
{
    return cur.h;
}


/*
When |hlist_out| is called, its duty is to output the box represented
by the |hlist_node| pointed to by |temp_ptr|. The reference point of that
box has coordinates |(cur.h,cur.v)|.

Similarly, when |vlist_out| is called, its duty is to output the box represented
by the |vlist_node| pointed to by |temp_ptr|. The reference point of that
box has coordinates |(cur.h,cur.v)|.
@^recursion@>
*/

/*
The recursive procedures |hlist_out| and |vlist_out| each have a local variable
|save_dvi| to hold the value of |dvi| just before
entering a new level of recursion.  In effect, the value of |save_dvi|
on \TeX's run-time stack corresponds to the values of |h| and |v|
that a \.{DVI}-reading program will push onto its coordinate stack.
*/

void hlist_out(void)
{                               /* output an |hlist_node| box */
    scaled base_line;           /* the baseline coordinate for this box */
    scaled c_wd, c_ht, c_dp;    /* the real width, height and depth of the character */
    scaled w;                   /*  temporary value for directional width calculation  */
    scaled edge_v;
    scaled edge_h;
    scaled effective_horizontal;
    scaled basepoint_horizontal;
    scaled basepoint_vertical = 0;
    integer save_direction;
    pointer dir_ptr, dir_tmp;
    scaled left_edge;           /* the left coordinate for this box */
    scaled save_h;              /* what |cur.h| should pop to */
    scaledpos save_dvi;         /* what |dvi| should pop to */
    scaledpos save_box_pos;     /* what |box_pos| should pop to */
    pointer this_box;           /* pointer to containing box */
    glue_ord g_order;           /* applicable order of infinity for glue */
    int g_sign;                 /* selects type of glue */
    pointer p, q;               /* current position in the hlist */
    integer save_loc;           /* \.{DVI} byte location upon entry */
    pointer leader_box;         /* the leader box being replicated */
    scaled leader_wd;           /* width of leader box being replicated */
    scaled lx;                  /* extra space between leader boxes */
    boolean outer_doing_leaders;        /* were we doing leaders? */
    scaled edge;                /* right edge of sub-box or leader space */
    real glue_temp;             /* glue value before rounding */
    real cur_glue;              /* glue seen so far */
    scaled cur_g;               /* rounded equivalent of |cur_glue| times the glue ratio */
    charinfo_short ci;          /* this is a caching attempt */
    cur_g = 0;
    cur_glue = 0.0;
    this_box = temp_ptr;
    g_order = glue_order(this_box);
    g_sign = glue_sign(this_box);
    p = list_ptr(this_box);
    set_to_zero(cur);
    box_pos = pos;
    save_direction = dvi_direction;
    dvi_direction = box_dir(this_box);
    /* DIR: Initialize |dir_ptr| for |ship_out| */
    {
        dir_ptr = null;
        push_dir(dvi_direction);
        dir_dvi_ptr(dir_ptr) = dvi_ptr;
    }
    incr(cur_s);
    if (cur_s > 0)
        dvi_out(push);
    if (cur_s > max_push)
        max_push = cur_s;
    save_loc = dvi_offset + dvi_ptr;
    base_line = cur.v;
    left_edge = cur.h;
    /* Start hlist {\sl Sync\TeX} information record */
    synctex_hlist(this_box);
    while (p != null) {
        /* Output node |p| for |hlist_out| and move to the next node,
           maintaining the condition |cur.v=base_line| */
        /*
           We ought to give special care to the efficiency of one part of |hlist_out|,
           since it belongs to \TeX's inner loop. When a |char_node| is encountered,
           we save a little time by processing several nodes in succession until
           reaching a non-|char_node|.
           @^inner loop@>
         */

        if (is_char_node(p)) {
            do {
                if (x_displace(p) != 0)
                    cur.h = cur.h + x_displace(p);
                if (y_displace(p) != 0)
                    cur.v = cur.v - y_displace(p);
                synch_pos_with_cur();
                f = font(p);
                c = character(p);
                ci = get_charinfo_short(f, c);
                if (f != dvi_f) {
                    /* Change font |dvi_f| to |f| */
                    if (!font_used(f)) {
                        dvi_font_def(f);
                        set_font_used(f, true);
                    }
                    oval = f - 1;
                    ocmd = fnt1;
                    out_cmd();
                    dvi_f = f;
                }
                c_ht = charinfo_height(ci);
                c_dp = charinfo_depth(ci);
                c_wd = charinfo_width(ci);
                if (font_natural_dir(f) != -1) {
                    switch (font_direction(dvi_direction)) {
                    case dir__LT:
                    case dir__LB:
                        dvi_set(c, c_wd);
                        break;
                    case dir__RT:
                    case dir__RB:
                        dvi_put(c);
                        break;
                    case dir__TL:
                    case dir__TR:
                    case dir__BL:
                    case dir__BR:
                        c_wd = c_ht + c_dp;
                        dvi_put(c);
                        break;
                    case dir__LL:
                        pos_right(c_wd);
                        dvi_put(c);
                        break;
                    case dir__RR:
                        pos_left(c_wd);
                        dvi_put(c);
                        break;
                    case dir__LR:
                        dvi_set(c, c_wd);
                        break;
                    case dir__RL:
                        dvi_put(c);
                        break;
                    case dir__TT:
                        c_wd = c_ht + c_dp;
                        pos_down(c_wd);
                        dvi_put(c);
                        break;
                    case dir__TB:
                        c_wd = c_ht + c_dp;
                        dvi_put(c);
                        break;
                    case dir__BT:
                        c_wd = c_ht + c_dp;
                        dvi_put(c);
                        break;
                    case dir__BB:
                        c_wd = c_ht + c_dp;
                        pos_up(c_wd);
                        dvi_put(c);
                        break;
                    }
                } else {
                    switch (font_direction(dvi_direction)) {
                    case dir__LT:
                        dvi_set(c, c_wd);
                        break;
                    case dir__LB:
                        pos_down(c_ht);
                        dvi_set(c, c_wd);
                        break;
                    case dir__RT:
                        pos_left(c_wd);
                        dvi_put(c);
                        break;
                    case dir__RB:
                        pos_down(c_ht);
                        pos_left(c_wd);
                        dvi_put(c);
                        break;
                    case dir__TL:
                        pos_down(c_ht);
                        pos_left(c_wd);
                        dvi_put(c);
                        c_wd = c_ht + c_dp;
                        break;
                    case dir__TR:
                        pos_down(c_ht);
                        dvi_put(c);
                        c_wd = c_ht + c_dp;
                        break;
                    case dir__BL:
                        pos_left(c_wd);
                        pos_up(c_dp);
                        dvi_put(c);
                        c_wd = c_ht + c_dp;
                        break;
                    case dir__BR:
                        pos_up(c_dp);
                        dvi_put(c);
                        c_wd = c_ht + c_dp;
                        break;
                    case dir__LL:
                    case dir__LR:
                        pos_down((c_ht - c_dp) / 2);
                        dvi_set(c, c_wd);
                        break;
                    case dir__RL:
                    case dir__RR:
                        pos_left(c_wd);
                        pos_down((c_ht - c_dp) / 2);
                        dvi_put(c);
                        break;
                    case dir__TT:
                    case dir__TB:
                        pos_down(c_ht);
                        pos_left(c_wd / 2);
                        dvi_put(c);
                        c_wd = c_ht + c_dp;
                        break;
                    case dir__BT:
                    case dir__BB:
                        pos_up(c_dp);
                        pos_left(c_wd / 2);
                        dvi_put(c);
                        c_wd = c_ht + c_dp;
                        break;
                    }
                }
                if (x_displace(p) != 0)
                    cur.h = cur.h - x_displace(p);
                if (y_displace(p) != 0)
                    cur.v = cur.v + y_displace(p);
                cur.h = cur.h + c_wd;
                p = vlink(p);
            } while (is_char_node(p));
            /* Record current point {\sl Sync\TeX} information */
            synctex_current();
        } else {
            /* Output the non-|char_node| |p| for |hlist_out|
               and move to the next node */

            switch (type(p)) {
            case hlist_node:
            case vlist_node:
                /* Output a box in an hlist */

                if (!
                    (dir_orthogonal
                     (dir_primary[box_dir(p)], dir_primary[dvi_direction]))) {
                    effective_horizontal = width(p);
                    basepoint_vertical = 0;
                    if (dir_opposite
                        (dir_secondary[box_dir(p)],
                         dir_secondary[dvi_direction]))
                        basepoint_horizontal = width(p);
                    else
                        basepoint_horizontal = 0;
                } else {
                    effective_horizontal = height(p) + depth(p);
                    if (!is_mirrored(box_dir(p))) {
                        if (dir_eq
                            (dir_primary[box_dir(p)],
                             dir_secondary[dvi_direction]))
                            basepoint_horizontal = height(p);
                        else
                            basepoint_horizontal = depth(p);
                    } else {
                        if (dir_eq
                            (dir_primary[box_dir(p)],
                             dir_secondary[dvi_direction]))
                            basepoint_horizontal = depth(p);
                        else
                            basepoint_horizontal = height(p);
                    }
                    if (dir_eq
                        (dir_secondary[box_dir(p)], dir_primary[dvi_direction]))
                        basepoint_vertical = -(width(p) / 2);
                    else
                        basepoint_vertical = (width(p) / 2);
                }
                if (!is_mirrored(dvi_direction))
                    basepoint_vertical = basepoint_vertical + shift_amount(p);  /* shift the box `down' */
                else
                    basepoint_vertical = basepoint_vertical - shift_amount(p);  /* shift the box `up' */
                if (list_ptr(p) == null) {
                    /* Record void list {\sl Sync\TeX} information */
                    if (type(p) == vlist_node)
                        synctex_void_vlist(p, this_box);
                    else
                        synctex_void_hlist(p, this_box);

                    cur.h = cur.h + effective_horizontal;
                } else {
                    temp_ptr = p;
                    edge = cur.h;
                    cur.h = cur.h + basepoint_horizontal;
                    edge_v = cur.v;
                    cur.v = base_line + basepoint_vertical;
                    synch_dvi_with_cur();
                    save_dvi = dvi;
                    save_box_pos = box_pos;
                    if (type(p) == vlist_node)
                        vlist_out();
                    else
                        hlist_out();
                    dvi = save_dvi;
                    box_pos = save_box_pos;
                    cur.h = edge + effective_horizontal;
                    cur.v = base_line;
                }
                break;
            case rule_node:
                if (!
                    (dir_orthogonal
                     (dir_primary[rule_dir(p)], dir_primary[dvi_direction]))) {
                    rule_ht = height(p);
                    rule_dp = depth(p);
                    rule_wd = width(p);
                } else {
                    rule_ht = width(p) / 2;
                    rule_dp = width(p) / 2;
                    rule_wd = height(p) + depth(p);
                }
                goto FIN_RULE;
                break;
            case whatsit_node:
                /* Output the whatsit node |p| in an hlist */
                if (subtype(p) != dir_node) {
                    out_what(p);
                } else {
                    /* Output a reflection instruction if the direction has changed */
                    if (dir_dir(p) >= 0) {
                        push_dir_node(p);
                        calculate_width_to_enddir(p, cur_glue, cur_g, this_box,
                                                  &w, &temp_ptr);
                        if ((dir_opposite
                             (dir_secondary[dir_dir(dir_ptr)],
                              dir_secondary[dvi_direction]))
                            ||
                            (dir_eq
                             (dir_secondary[dir_dir(dir_ptr)],
                              dir_secondary[dvi_direction]))) {
                            dir_cur_h(temp_ptr) = cur.h + w;
                            if (dir_opposite
                                (dir_secondary[dir_dir(dir_ptr)],
                                 dir_secondary[dvi_direction]))
                                cur.h = cur.h + w;
                        } else {
                            dir_cur_h(temp_ptr) = cur.h;
                        }
                        dir_cur_v(temp_ptr) = cur.v;
                        dir_box_pos_h(temp_ptr) = box_pos.h;
                        dir_box_pos_v(temp_ptr) = box_pos.v;
                        synch_pos_with_cur();   /* no need for |synch_dvi_with_cur|, as there is no DVI grouping */
                        box_pos = pos;  /* fake a nested |hlist_out| */
                        set_to_zero(cur);
                        dvi_direction = dir_dir(dir_ptr);
                    } else {
                        pop_dir_node();
                        box_pos.h = dir_box_pos_h(p);
                        box_pos.v = dir_box_pos_v(p);
                        cur.h = dir_cur_h(p);
                        cur.v = dir_cur_v(p);
                        if (dir_ptr != null)
                            dvi_direction = dir_dir(dir_ptr);
                        else
                            dvi_direction = 0;
                    }
                }

                break;
            case glue_node:
                /* Move right or output leaders */
                g = glue_ptr(p);
                rule_wd = width(g) - cur_g;
                if (g_sign != normal) {
                    if (g_sign == stretching) {
                        if (stretch_order(g) == g_order) {
                            cur_glue = cur_glue + stretch(g);
                            vet_glue(float_cast(glue_set(this_box)) * cur_glue);
                            cur_g = float_round(glue_temp);
                        }
                    } else if (shrink_order(g) == g_order) {
                        cur_glue = cur_glue - shrink(g);
                        vet_glue(float_cast(glue_set(this_box)) * cur_glue);
                        cur_g = float_round(glue_temp);
                    }
                }
                rule_wd = rule_wd + cur_g;
                if (subtype(p) >= a_leaders) {
                    /* Output leaders in an hlist, |goto fin_rule| if a rule
                       or to |next_p| if done */
                    leader_box = leader_ptr(p);
                    if (type(leader_box) == rule_node) {
                        rule_ht = height(leader_box);
                        rule_dp = depth(leader_box);
                        goto FIN_RULE;
                    }
                    if (!
                        (dir_orthogonal
                         (dir_primary[box_dir(leader_box)],
                          dir_primary[dvi_direction])))
                        leader_wd = width(leader_box);
                    else
                        leader_wd = height(leader_box) + depth(leader_box);
                    if ((leader_wd > 0) && (rule_wd > 0)) {
                        rule_wd = rule_wd + 10; /* compensate for floating-point rounding */
                        edge = cur.h + rule_wd;
                        lx = 0;
                        /* Let |cur.h| be the position of the first box, and set |leader_wd+lx|
                           to the spacing between corresponding parts of boxes */
                        /* The calculations related to leaders require a bit of care. First, in the
                           case of |a_leaders| (aligned leaders), we want to move |cur.h| to
                           |left_edge| plus the smallest multiple of |leader_wd| for which the result
                           is not less than the current value of |cur.h|; i.e., |cur.h| should become
                           $|left_edge|+|leader_wd|\times\lceil
                           (|cur.h|-|left_edge|)/|leader_wd|\rceil$.  The program here should work in
                           all cases even though some implementations of \PASCAL\ give nonstandard
                           results for the |div| operation when |cur.h| is less than |left_edge|.

                           In the case of |c_leaders| (centered leaders), we want to increase |cur.h|
                           by half of the excess space not occupied by the leaders; and in the
                           case of |x_leaders| (expanded leaders) we increase |cur.h|
                           by $1/(q+1)$ of this excess space, where $q$ is the number of times the
                           leader box will be replicated. Slight inaccuracies in the division might
                           accumulate; half of this rounding error is placed at each end of the leaders. */

                        if (subtype(p) == a_leaders) {
                            save_h = cur.h;
                            cur.h =
                                left_edge +
                                leader_wd * ((cur.h - left_edge) / leader_wd);
                            if (cur.h < save_h)
                                cur.h = cur.h + leader_wd;
                        } else {
                            lq = rule_wd / leader_wd;   /* the number of box copies */
                            lr = rule_wd % leader_wd;   /* the remaining space */
                            if (subtype(p) == c_leaders) {
                                cur.h = cur.h + (lr / 2);
                            } else {
                                lx = lr / (lq + 1);
                                cur.h = cur.h + ((lr - (lq - 1) * lx) / 2);
                            }
                        }
                        while (cur.h + leader_wd <= edge) {
                            /* Output a leader box at |cur.h|,
                               then advance |cur.h| by |leader_wd+lx| */
                            /* The `\\{synch}' operations here are intended to decrease the number of
                               bytes needed to specify horizontal and vertical motion in the \.{DVI} output. */

                            if (!
                                (dir_orthogonal
                                 (dir_primary[box_dir(leader_box)],
                                  dir_primary[dvi_direction]))) {
                                basepoint_vertical = 0;
                                if (dir_opposite
                                    (dir_secondary[box_dir(leader_box)],
                                     dir_secondary[dvi_direction]))
                                    basepoint_horizontal = width(leader_box);
                                else
                                    basepoint_horizontal = 0;
                            } else {
                                if (!is_mirrored(box_dir(leader_box))) {
                                    if (dir_eq
                                        (dir_primary[box_dir(leader_box)],
                                         dir_secondary[dvi_direction]))
                                        basepoint_horizontal =
                                            height(leader_box);
                                    else
                                        basepoint_horizontal =
                                            depth(leader_box);
                                } else {
                                    if (dir_eq
                                        (dir_primary[box_dir(leader_box)],
                                         dir_secondary[dvi_direction]))
                                        basepoint_horizontal =
                                            depth(leader_box);
                                    else
                                        basepoint_horizontal =
                                            height(leader_box);
                                }
                                if (dir_eq
                                    (dir_secondary[box_dir(leader_box)],
                                     dir_primary[dvi_direction]))
                                    basepoint_vertical =
                                        -(width(leader_box) / 2);
                                else
                                    basepoint_vertical =
                                        (width(leader_box) / 2);
                            }
                            if (!is_mirrored(dvi_direction))
                                basepoint_vertical = basepoint_vertical + shift_amount(leader_box);     /* shift the box `down' */
                            else
                                basepoint_vertical = basepoint_vertical - shift_amount(leader_box);     /* shift the box `up' */
                            temp_ptr = leader_box;
                            edge_h = cur.h;
                            cur.h = cur.h + basepoint_horizontal;
                            edge_v = cur.v;
                            cur.v = base_line + basepoint_vertical;
                            synch_dvi_with_cur();
                            save_dvi = dvi;
                            save_box_pos = box_pos;
                            outer_doing_leaders = doing_leaders;
                            doing_leaders = true;
                            if (type(leader_box) == vlist_node)
                                vlist_out();
                            else
                                hlist_out();
                            doing_leaders = outer_doing_leaders;
                            box_pos = save_box_pos;
                            dvi = save_dvi;
                            cur.h = edge_h + leader_wd + lx;
                            cur.v = base_line;
                        }
                        cur.h = edge - 10;
                        goto NEXT_P;
                    }
                }
                goto MOVE_PAST;
                break;
            case disc_node:
                if (vlink(no_break(p)) != null) {
                    if (subtype(p) != select_disc) {
                        vlink(tlink(no_break(p))) = vlink(p);
                        q = vlink(no_break(p));
                        vlink(no_break(p)) = null;
                        vlink(p) = q;
                    }
                }
                break;
            case margin_kern_node:
                cur.h = cur.h + width(p);
                break;
            case kern_node:
                /* Record |kern_node| {\sl Sync\TeX} information */
                synctex_kern(p, this_box);
                cur.h = cur.h + width(p);
                break;
            case math_node:
                /* Record |math_node| {\sl Sync\TeX} information */
                synctex_math(p, this_box);
                cur.h = cur.h + surround(p);
                break;
            default:
                break;
            }
            goto NEXT_P;
          FIN_RULE:
            /* Output a rule in an hlist */
            if (is_running(rule_ht))
                rule_ht = height(this_box);
            if (is_running(rule_dp))
                rule_dp = depth(this_box);
            rule_ht = rule_ht + rule_dp;        /* this is the rule thickness */
            if ((rule_ht > 0) && (rule_wd > 0)) {       /* we don't output empty rules */
                cur.v = base_line + rule_dp;
                synch_pos_with_cur();
                switch (box_direction(dvi_direction)) {
                case dir_TL_:
                    dvi_set_rule(rule_ht, rule_wd);
                    break;
                case dir_BL_:
                    pos_down(rule_ht);
                    dvi_set_rule(rule_ht, rule_wd);
                    break;
                case dir_TR_:
                    pos_left(rule_wd);
                    dvi_put_rule(rule_ht, rule_wd);
                    break;
                case dir_BR_:
                    pos_left(rule_wd);
                    pos_down(rule_ht);
                    dvi_put_rule(rule_ht, rule_wd);
                    break;
                case dir_LT_:
                    pos_down(rule_wd);
                    pos_left(rule_ht);
                    dvi_set_rule(rule_wd, rule_ht);
                    break;
                case dir_RT_:
                    pos_down(rule_wd);
                    dvi_put_rule(rule_wd, rule_ht);
                    break;
                case dir_LB_:
                    pos_left(rule_ht);
                    dvi_set_rule(rule_wd, rule_ht);
                    break;
                case dir_RB_:
                    dvi_put_rule(rule_wd, rule_ht);
                    break;
                }
                cur.v = base_line;
            }
          MOVE_PAST:
            cur.h = cur.h + rule_wd;
            /* Record horizontal |rule_node| or |glue_node| {\sl Sync\TeX} information */
            synctex_horizontal_rule_or_glue(p, this_box);
          NEXT_P:
            p = vlink(p);
        }
    }
    /* Finish hlist {\sl Sync\TeX} information record */
    synctex_tsilh(this_box);
    prune_movements(save_loc);
    if (cur_s > 0)
        dvi_pop(save_loc);
    decr(cur_s);
    dvi_direction = save_direction;
    /* DIR: Reset |dir_ptr| */
    while (dir_ptr != null)
        pop_dir_node();
}


/* The |vlist_out| routine is similar to |hlist_out|, but a bit simpler */

void vlist_out(void)
{                               /* output a |vlist_node| box */

    scaled left_edge;           /* the left coordinate for this box */
    scaled top_edge;            /* the top coordinate for this box */
    scaled save_v;              /* what |cur.v| should pop to */
    scaledpos save_dvi;         /* what |dvi| should pop to */
    scaledpos save_box_pos;     /* what |box_pos| should pop to */
    pointer this_box;           /* pointer to containing box */
    glue_ord g_order;           /* applicable order of infinity for glue */
    int g_sign;                 /* selects type of glue */
    pointer p;                  /* current position in the vlist */
    integer save_loc;           /* \.{DVI} byte location upon entry */
    pointer leader_box;         /* the leader box being replicated */
    scaled leader_ht;           /* height of leader box being replicated */
    scaled lx;                  /* extra space between leader boxes */
    boolean outer_doing_leaders;        /* were we doing leaders? */
    scaled edge;                /* bottom boundary of leader space */
    real glue_temp;             /* glue value before rounding */
    real cur_glue;              /* glue seen so far */
    scaled cur_g;               /* rounded equivalent of |cur_glue| times the glue ratio */
    integer save_direction;
    scaled effective_vertical;
    scaled basepoint_horizontal;
    scaled basepoint_vertical = 0;      /* FIXME: init is only done to silence a compiler warning */
    scaled edge_v;
    cur_g = 0;
    cur_glue = 0.0;
    this_box = temp_ptr;
    g_order = glue_order(this_box);
    g_sign = glue_sign(this_box);
    p = list_ptr(this_box);
    set_to_zero(cur);
    box_pos = pos;
    save_direction = dvi_direction;
    dvi_direction = box_dir(this_box);
    incr(cur_s);
    if (cur_s > 0)
        dvi_out(push);
    if (cur_s > max_push)
        max_push = cur_s;
    save_loc = dvi_offset + dvi_ptr;
    left_edge = cur.h;
    /* Start vlist {\sl Sync\TeX} information record */
    synctex_vlist(this_box);
    cur.v = cur.v - height(this_box);
    top_edge = cur.v;
    while (p != null) {
        /* Output node |p| for |vlist_out| and move to the next node,
           maintaining the condition |cur.h=left_edge| */
        if (is_char_node(p)) {
            print_font_and_char(p);
            tconfusion("vlistout");
        } else {
            /* Output the non-|char_node| |p| for |vlist_out| */
            switch (type(p)) {
            case hlist_node:
            case vlist_node:
                /* Output a box in a vlist */
                /* The |synch_v| here allows the \.{DVI} output to use one-byte commands
                   for adjusting |v| in most cases, since the baselineskip distance will
                   usually be constant. */
                if (!
                    (dir_orthogonal
                     (dir_primary[box_dir(p)], dir_primary[dvi_direction]))) {
                    effective_vertical = height(p) + depth(p);
                    if ((type(p) == hlist_node) && is_mirrored(box_dir(p)))
                        basepoint_vertical = depth(p);
                    else
                        basepoint_vertical = height(p);
                    if (dir_opposite
                        (dir_secondary[box_dir(p)],
                         dir_secondary[dvi_direction]))
                        basepoint_horizontal = width(p);
                    else
                        basepoint_horizontal = 0;
                } else {
                    effective_vertical = width(p);
                    if (!is_mirrored(box_dir(p))) {
                        if (dir_eq
                            (dir_primary[box_dir(p)],
                             dir_secondary[dvi_direction]))
                            basepoint_horizontal = height(p);
                        else
                            basepoint_horizontal = depth(p);
                    } else {
                        if (dir_eq
                            (dir_primary[box_dir(p)],
                             dir_secondary[dvi_direction]))
                            basepoint_horizontal = depth(p);
                        else
                            basepoint_horizontal = height(p);
                        if (dir_eq
                            (dir_secondary[box_dir(p)],
                             dir_primary[dvi_direction]))
                            basepoint_vertical = 0;
                        else
                            basepoint_vertical = width(p);
                    }
                }
                basepoint_horizontal = basepoint_horizontal + shift_amount(p);  /* shift the box `right' */
                if (list_ptr(p) == null) {
                    cur.v = cur.v + effective_vertical;
                    /* Record void list {\sl Sync\TeX} information */
                    if (type(p) == vlist_node)
                        synctex_void_vlist(p, this_box);
                    else
                        synctex_void_hlist(p, this_box);

                } else {
                    edge_v = cur.v;
                    cur.h = left_edge + basepoint_horizontal;
                    cur.v = cur.v + basepoint_vertical;
                    synch_dvi_with_cur();
                    save_dvi = dvi;
                    save_box_pos = box_pos;
                    temp_ptr = p;
                    if (type(p) == vlist_node)
                        vlist_out();
                    else
                        hlist_out();
                    dvi = save_dvi;
                    box_pos = save_box_pos;
                    cur.h = left_edge;
                    cur.v = edge_v + effective_vertical;
                }
                break;
            case rule_node:
                if (!
                    (dir_orthogonal
                     (dir_primary[rule_dir(p)], dir_primary[dvi_direction]))) {
                    rule_ht = height(p);
                    rule_dp = depth(p);
                    rule_wd = width(p);
                } else {
                    rule_ht = width(p) / 2;
                    rule_dp = width(p) / 2;
                    rule_wd = height(p) + depth(p);
                }
                goto FIN_RULE;
                break;
            case whatsit_node:
                /* Output the whatsit node |p| in a vlist */
                out_what(p);
                break;
            case glue_node:
                /* Move down or output leaders */
                g = glue_ptr(p);
                rule_ht = width(g) - cur_g;
                if (g_sign != normal) {
                    if (g_sign == stretching) {
                        if (stretch_order(g) == g_order) {
                            cur_glue = cur_glue + stretch(g);
                            vet_glue(float_cast(glue_set(this_box)) * cur_glue);
                            cur_g = float_round(glue_temp);
                        }
                    } else if (shrink_order(g) == g_order) {
                        cur_glue = cur_glue - shrink(g);
                        vet_glue(float_cast(glue_set(this_box)) * cur_glue);
                        cur_g = float_round(glue_temp);
                    }
                }
                rule_ht = rule_ht + cur_g;
                if (subtype(p) >= a_leaders) {
                    /* Output leaders in a vlist, |goto fin_rule| if a rule
                       or to |next_p| if done */
                    leader_box = leader_ptr(p);
                    if (type(leader_box) == rule_node) {
                        rule_wd = width(leader_box);
                        rule_dp = 0;
                        goto FIN_RULE;
                    }
                    if (!
                        (dir_orthogonal
                         (dir_primary[box_dir(leader_box)],
                          dir_primary[dvi_direction])))
                        leader_ht = height(leader_box) + depth(leader_box);
                    else
                        leader_ht = width(leader_box);
                    if ((leader_ht > 0) && (rule_ht > 0)) {
                        rule_ht = rule_ht + 10; /* compensate for floating-point rounding */
                        edge = cur.v + rule_ht;
                        lx = 0;
                        /* Let |cur.v| be the position of the first box, and set |leader_ht+lx|
                           to the spacing between corresponding parts of boxes */
                        if (subtype(p) == a_leaders) {
                            save_v = cur.v;
                            cur.v =
                                top_edge +
                                leader_ht * ((cur.v - top_edge) / leader_ht);
                            if (cur.v < save_v)
                                cur.v = cur.v + leader_ht;
                        } else {
                            lq = rule_ht / leader_ht;   /* the number of box copies */
                            lr = rule_ht % leader_ht;   /* the remaining space */
                            if (subtype(p) == c_leaders) {
                                cur.v = cur.v + (lr / 2);
                            } else {
                                lx = lr / (lq + 1);
                                cur.v = cur.v + ((lr - (lq - 1) * lx) / 2);
                            }
                        }

                        while (cur.v + leader_ht <= edge) {
                            /* Output a leader box at |cur.v|, then advance |cur.v| by |leader_ht+lx| */

                            /* When we reach this part of the program, |cur.v| indicates the top of a
                               leader box, not its baseline. */

                            if (!
                                (dir_orthogonal
                                 (dir_primary[box_dir(leader_box)],
                                  dir_primary[dvi_direction]))) {
                                effective_vertical =
                                    height(leader_box) + depth(leader_box);
                                if ((type(leader_box) == hlist_node)
                                    && is_mirrored(box_dir(leader_box)))
                                    basepoint_vertical = depth(leader_box);
                                else
                                    basepoint_vertical = height(leader_box);
                                if (dir_opposite
                                    (dir_secondary[box_dir(leader_box)],
                                     dir_secondary[dvi_direction]))
                                    basepoint_horizontal = width(leader_box);
                                else
                                    basepoint_horizontal = 0;
                            } else {
                                effective_vertical = width(leader_box);
                                if (!is_mirrored(box_dir(leader_box))) {
                                    if (dir_eq
                                        (dir_primary[box_dir(leader_box)],
                                         dir_secondary[dvi_direction]))
                                        basepoint_horizontal =
                                            height(leader_box);
                                    else
                                        basepoint_horizontal =
                                            depth(leader_box);
                                } else {
                                    if (dir_eq
                                        (dir_primary[box_dir(leader_box)],
                                         dir_secondary[dvi_direction]))
                                        basepoint_horizontal =
                                            depth(leader_box);
                                    else
                                        basepoint_horizontal =
                                            height(leader_box);
                                }
                                if (dir_eq
                                    (dir_secondary[box_dir(leader_box)],
                                     dir_primary[dvi_direction]))
                                    basepoint_vertical = width(leader_box);
                                else
                                    basepoint_vertical = 0;
                            }
                            basepoint_horizontal =
                                basepoint_horizontal + shift_amount(leader_box);
                            /* shift the box `right' */
                            temp_ptr = leader_box;
                            cur.h = left_edge + basepoint_horizontal;
                            edge_v = cur.v;
                            cur.v = cur.v + basepoint_vertical;
                            synch_dvi_with_cur();
                            save_dvi = dvi;
                            save_box_pos = box_pos;
                            outer_doing_leaders = doing_leaders;
                            doing_leaders = true;
                            if (type(leader_box) == vlist_node)
                                vlist_out();
                            else
                                hlist_out();
                            doing_leaders = outer_doing_leaders;
                            box_pos = save_box_pos;
                            dvi = save_dvi;
                            cur.h = left_edge;
                            cur.v = edge_v + leader_ht + lx;
                        }
                        cur.v = edge - 10;
                        goto NEXT_P;
                    }
                    goto MOVE_PAST;
                }
                break;
            case kern_node:
                cur.v = cur.v + width(p);
                break;
            default:
                break;
            }
            goto NEXT_P;

          FIN_RULE:
            /* Output a rule in a vlist, |goto next_p| */
            if (is_running(rule_wd))
                rule_wd = width(this_box);
            rule_ht = rule_ht + rule_dp;        /* this is the rule thickness */
            cur.v = cur.v + rule_ht;
            if ((rule_ht > 0) && (rule_wd > 0)) {       /* we don't output empty rules */
                synch_pos_with_cur();
                switch (box_direction(dvi_direction)) {
                case dir_TL_:
                    dvi_put_rule(rule_ht, rule_wd);
                    break;
                case dir_BL_:
                    pos_down(rule_ht);
                    dvi_put_rule(rule_ht, rule_wd);
                    break;
                case dir_TR_:
                    pos_left(rule_wd);
                    dvi_set_rule(rule_ht, rule_wd);
                    break;
                case dir_BR_:
                    pos_down(rule_ht);
                    pos_left(rule_wd);
                    dvi_set_rule(rule_ht, rule_wd);
                    break;
                case dir_LT_:
                    pos_down(rule_wd);
                    pos_left(rule_ht);
                    dvi_set_rule(rule_wd, rule_ht);
                    break;
                case dir_RT_:
                    pos_down(rule_wd);
                    dvi_put_rule(rule_wd, rule_ht);
                    break;
                case dir_LB_:
                    pos_left(rule_ht);
                    dvi_set_rule(rule_wd, rule_ht);
                    break;
                case dir_RB_:
                    dvi_put_rule(rule_wd, rule_ht);
                    break;
                }
            }
            goto NEXT_P;

          MOVE_PAST:
            cur.v = cur.v + rule_ht;

          NEXT_P:
            p = vlink(p);
        }
    }
    /* Finish vlist {\sl Sync\TeX} information record */
    synctex_tsilv(this_box);
    prune_movements(save_loc);
    if (cur_s > 0)
        dvi_pop(save_loc);
    decr(cur_s);
    dvi_direction = save_direction;
}

/*
Here's an example of how these conventions are used. Whenever it is time to
ship out a box of stuff, we shall use the macro |ensure_dvi_open|.
*/

void ensure_dvi_open(void)
{
    if (output_file_name == 0) {
        if (job_name == 0)
            open_log_file();
        pack_job_name(".dvi");
        while (!lua_b_open_out(dvi_file))
            prompt_file_name("file name for output", ".dvi");
        dvi_file = name_file_pointer;
        output_file_name = make_name_string();
    }
}


void special_out(halfword p)
{
    int old_setting;            /* holds print |selector| */
    pool_pointer k;             /* index into |str_pool| */
    synch_dvi_with_cur();
    old_setting = selector;
    selector = new_string;
    show_token_list(link(write_tokens(p)), null, pool_size - pool_ptr);
    selector = old_setting;
    str_room(1);
    if (cur_length < 256) {
        dvi_out(xxx1);
        dvi_out(cur_length);
    } else {
        dvi_out(xxx4);
        dvi_four(cur_length);
    }
    for (k = str_start_macro(str_ptr); k <= pool_ptr - 1; k++)
        dvi_out(str_pool[k]);
    pool_ptr = str_start_macro(str_ptr);        /* erase the string */
}

/*
The final line of this routine is slightly subtle; at least, the author
didn't think about it until getting burnt! There is a used-up token list
@^Knuth, Donald Ervin@>
on the stack, namely the one that contained |end_write_token|. (We
insert this artificial `\.{\\endwrite}' to prevent runaways, as explained
above.) If it were not removed, and if there were numerous writes on a
single page, the stack would overflow.
*/

/* TODO: keep track of this value */
#define end_write_token cs_token_flag+end_write

void expand_macros_in_tokenlist(halfword p)
{
    integer old_mode;           /* saved |mode| */
    pointer q, r;               /* temporary variables for list manipulation */
    integer end_write = get_nullcs() + 1 + get_hash_size();     /* hashbase=nullcs+1 */
    end_write += 8;             /* end_write=frozen_control_sequence+8 */
    q = get_avail();
    info(q) = right_brace_token + '}';
    r = get_avail();
    link(q) = r;
    info(r) = end_write_token;
    begin_token_list(q, inserted);
    begin_token_list(write_tokens(p), write_text);
    q = get_avail();
    info(q) = left_brace_token + '{';
    begin_token_list(q, inserted);
    /* now we're ready to scan
       `\.\{$\langle\,$token list$\,\rangle$\.{\} \\endwrite}' */
    old_mode = mode;
    mode = 0;
    /* disable \.{\\prevdepth}, \.{\\spacefactor}, \.{\\lastskip}, \.{\\prevgraf} */
    cur_cs = write_loc;
    q = scan_toks(false, true); /* expand macros, etc. */
    get_token();
    if (cur_tok != end_write_token) {
        /* Recover from an unbalanced write command */
        char *hlp[] = {
            "On this page there's a \\write with fewer real {'s than }'s.",
            "I can't handle that very well; good luck.", NULL
        };
        tex_error("Unbalanced write command", hlp);
        do {
            get_token();
        } while (cur_tok != end_write_token);
    }
    mode = old_mode;
    end_token_list();           /* conserve stack space */
}

void write_out(halfword p)
{
    int old_setting;            /* holds print |selector| */
    int j;                      /* write stream number */
    integer d;                  /* number of characters in incomplete current string */
    boolean clobbered;          /* system string is ok? */
    integer ret;                /* return value from |runsystem| */
    expand_macros_in_tokenlist(p);
    old_setting = selector;
    j = write_stream(p);
    if (j == 18) {
        selector = new_string;
    } else if (write_open[j]) {
        selector = j;
    } else {                    /* write to the terminal if file isn't open */
        if ((j == 17) && (selector == term_and_log))
            selector = log_only;
        tprint_nl("");
    }
    token_show(def_ref);
    print_ln();
    flush_list(def_ref);
    if (j == 18) {
        if (tracing_online <= 0)
            selector = log_only;        /* Show what we're doing in the log file. */
        else
            selector = term_and_log;    /* Show what we're doing. */
        /* If the log file isn't open yet, we can only send output to the terminal.
           Calling |open_log_file| from here seems to result in bad data in the log.
         */
        if (!log_opened)
            selector = term_only;
        tprint_nl("runsystem(");
        for (d = 0; d <= cur_length - 1; d++) {
            /* |print| gives up if passed |str_ptr|, so do it by hand. */
            print_char(str_pool[str_start_macro(str_ptr) + d]);
        }
        tprint(")...");
        if (shellenabledp) {
            str_room(1);
            append_char(0);     /* Append a null byte to the expansion. */
            clobbered = false;
            for (d = 0; d <= cur_length - 1; d++) {     /* Convert to external character set. */
                if ((str_pool[str_start_macro(str_ptr) + d] == 0)
                    && (d < cur_length - 1))
                    clobbered = true;
                /* minimal checking: NUL not allowed in argument string of |system|() */
            }
            if (clobbered) {
                tprint("clobbered");
            } else {
                /* We have the command.  See if we're allowed to execute it,
                   and report in the log.  We don't check the actual exit status of
                   the command, or do anything with the output. */
                ret =
                    runsystem(stringcast
                              (addressof(str_pool[str_start_macro(str_ptr)])));
                if (ret == -1)
                    tprint("quotation error in system command");
                else if (ret == 0)
                    tprint("disabled (restricted)");
                else if (ret == 1)
                    tprint("executed");
                else if (ret == 2)
                    tprint("executed (allowed)");
            }
        } else {
            tprint("disabled"); /* |shellenabledp| false */
        }
        print_char('.');
        tprint_nl("");
        print_ln();
        pool_ptr = str_start_macro(str_ptr);    /* erase the string */
    }
    selector = old_setting;
}




/*
The |out_what| procedure takes care of outputting whatsit nodes for
|vlist_out| and |hlist_out|. */

void out_what(halfword p)
{
    int j;                      /* write stream number */
    switch (subtype(p)) {
    case open_node:
    case write_node:
    case close_node:
        /* Do some work that has been queued up for \.{\\write} */
        /* We don't implement \.{\\write} inside of leaders. (The reason is that
           the number of times a leader box appears might be different in different
           implementations, due to machine-dependent rounding in the glue calculations.)
           @^leaders@> */
        if (!doing_leaders) {
            j = write_stream(p);
            if (subtype(p) == write_node) {
                write_out(p);
            } else {
                if (write_open[j])
                    lua_a_close_out(write_file[j]);
                if (subtype(p) == close_node) {
                    write_open[j] = false;
                } else if (j < 16) {
                    cur_name = open_name(p);
                    cur_area = open_area(p);
                    cur_ext = open_ext(p);
                    if (cur_ext == get_nullstr())
                        cur_ext = maketexstring(".tex");
                    pack_file_name(cur_name, cur_area, cur_ext);
                    while (!lua_a_open_out(write_file[j], (j + 1)))
                        prompt_file_name("output file name", ".tex");
                    write_file[j] = name_file_pointer;
                    write_open[j] = true;
                }
            }
        }
        break;
    case special_node:
        special_out(p);
        break;
    case pdf_save_pos_node:
        /* Save current position */
        synch_pos_with_cur();
        pdf_last_x_pos = pos.h;
        pdf_last_y_pos = pos.v;
        break;
    case local_par_node:
    case cancel_boundary_node:
    case user_defined_node:
        break;
    default:
        tconfusion("ext4");
        /* those will be pdf extension nodes in dvi mode, most likely */
        break;
    }
}




/* 
The |hlist_out| and |vlist_out| procedures are now complete, so we are
ready for the |dvi_ship_out| routine that gets them started in the first place.
*/

void dvi_ship_out(halfword p)
{
    /* output the box |p| */
    integer page_loc;           /* location of the current |bop| */
    int j, k;                   /* indices to first ten count registers */
    pool_pointer s;             /* index into |str_pool| */
    int old_setting;            /* saved |selector| setting */
    integer pre_callback_id;
    integer post_callback_id;
    boolean ret;
    integer count_base = get_count_base();

    if (half_buf == 0) {
        half_buf = dvi_buf_size / 2;
        dvi_limit = dvi_buf_size;
    }
    /* Start sheet {\sl Sync\TeX} information record */
    pdf_output_value = pdf_output;      /* {\sl Sync\TeX}: we assume that |pdf_output| is properly set up */
    synctex_sheet(mag);

    pre_callback_id = callback_defined(start_page_number_callback);
    post_callback_id = callback_defined(stop_page_number_callback);
    if ((tracing_output > 0) && (pre_callback_id == 0)) {
        tprint_nl("");
        print_ln();
        tprint("Completed box being shipped out");
    }
    if (pre_callback_id > 0) {
        ret = run_callback(pre_callback_id, "->");
    } else if (pre_callback_id == 0) {
        if (term_offset > max_print_line - 9)
            print_ln();
        else if ((term_offset > 0) || (file_offset > 0))
            print_char(' ');
        print_char('[');
        j = 9;
        while ((count(j) == 0) && (j > 0))
            decr(j);
        for (k = 0; k <= j; k++) {
            print_int(count(k));
            if (k < j)
                print_char('.');
        }
    }
    if ((tracing_output > 0) && (post_callback_id == 0)) {
        print_char(']');
        begin_diagnostic();
        show_box(p);
        end_diagnostic(true);
    }
    /* Ship box |p| out */
    if (box_dir(p) != page_direction)
        pdf_warning(maketexstring("\\shipout"),
                    maketexstring("\\pagedir != \\bodydir; "
                                  "\\box255 may be placed wrongly on the page."),
                    true, true);
    /* Update the values of |max_h| and |max_v|; but if the page is too large,
       |goto done| */
    /* Sometimes the user will generate a huge page because other error messages
       are being ignored. Such pages are not output to the \.{dvi} file, since they
       may confuse the printing software. */
    if ((height(p) > max_dimen) || (depth(p) > max_dimen) ||
        (height(p) + depth(p) + v_offset > max_dimen)
        || (width(p) + h_offset > max_dimen)) {
        char *hlp[] = { "The page just created is more than 18 feet tall or",
            "more than 18 feet wide, so I suspect something went wrong.",
            NULL
        };
        tex_error("Huge page cannot be shipped out", hlp);
        if (tracing_output <= 0) {
            begin_diagnostic();
            tprint_nl("The following box has been deleted:");
            show_box(p);
            end_diagnostic(true);
        }
        goto DONE;
    }
    if (height(p) + depth(p) + v_offset > max_v)
        max_v = height(p) + depth(p) + v_offset;
    if (width(p) + h_offset > max_h)
        max_h = width(p) + h_offset;

    /* Initialize variables as |ship_out| begins */
    set_to_zero(dvi);
    set_to_zero(cur);
    dvi_f = null_font;
    /* Calculate DVI page dimensions and margins */
    if (page_width > 0) {
        cur_page_size.h = page_width;
    } else {
        switch (box_direction(dvi_direction)) {
        case dir_TL_:
        case dir_BL_:
            cur_page_size.h = width(p) + 2 * page_left_offset;
            break;
        case dir_TR_:
        case dir_BR_:
            cur_page_size.h = width(p) + 2 * page_right_offset;
            break;
        case dir_LT_:
        case dir_LB_:
            cur_page_size.h = height(p) + depth(p) + 2 * page_left_offset;
            break;
        case dir_RT_:
        case dir_RB_:
            cur_page_size.h = height(p) + depth(p) + 2 * page_right_offset;
        }
    }
    if (page_height > 0) {
        cur_page_size.v = page_height;
    } else {
        switch (box_direction(dvi_direction)) {
        case dir_TL_:
        case dir_TR_:
            cur_page_size.v = height(p) + depth(p) + 2 * page_top_offset;
            break;
        case dir_BL_:
        case dir_BR_:
            cur_page_size.v = height(p) + depth(p) + 2 * page_bottom_offset;
            break;
        case dir_LT_:
        case dir_RT_:
            cur_page_size.v = width(p) + 2 * page_top_offset;
            break;
        case dir_LB_:
        case dir_RB_:
            cur_page_size.v = width(p) + 2 * page_bottom_offset;
        }
    }
    ensure_dvi_open();
    if (total_pages == 0) {
        dvi_out(pre);
        dvi_out(id_byte);       /* output the preamble */
        dvi_four(25400000);
        dvi_four(473628672);    /* conversion ratio for sp */
        prepare_mag();
        dvi_four(mag);          /* magnification factor is frozen */
        if (output_comment) {
            l = strlen(output_comment);
            dvi_out(l);
            for (s = 0; s <= l - 1; s++)
                dvi_out(output_comment[s]);
        } else {                /* the default code is unchanged */
            old_setting = selector;
            selector = new_string;
            tprint(" LuaTeX output ");
            print_int(int_par(param_year_code));
            print_char('.');
            print_two(int_par(param_month_code));
            print_char('.');
            print_two(int_par(param_day_code));
            print_char(':');
            print_two(int_par(param_time_code) / 60);
            print_two(int_par(param_time_code) % 60);
            selector = old_setting;
            dvi_out(cur_length);
            for (s = str_start_macro(str_ptr); s <= pool_ptr - 1; s++)
                dvi_out(str_pool[s]);
            pool_ptr = str_start_macro(str_ptr);        /* flush the current string */
        }
    }
    page_loc = dvi_offset + dvi_ptr;
    dvi_out(bop);
    for (k = 0; k <= 9; k++)
        dvi_four(count(k));
    dvi_four(last_bop);
    last_bop = page_loc;
    /* Think in upright page/paper coordinates): First preset |pos.h| and |pos.v| to the DVI origin. */
    pos.h = one_true_inch;
    pos.v = cur_page_size.v - one_true_inch;
    box_pos = pos;
    dvi = pos;
    /* Then calculate |cur.h| and |cur.v| within the upright coordinate system
       for the DVI origin depending on the |page_direction|. */
    dvi_direction = page_direction;
    switch (box_direction(dvi_direction)) {
    case dir_TL_:
    case dir_LT_:
        cur.h = h_offset;
        cur.v = v_offset;
        break;
    case dir_TR_:
    case dir_RT_:
        cur.h = cur_page_size.h - page_right_offset - one_true_inch;
        cur.v = v_offset;
        break;
    case dir_BL_:
    case dir_LB_:
        cur.h = h_offset;
        cur.v = cur_page_size.v - page_bottom_offset - one_true_inch;
        break;
    case dir_RB_:
    case dir_BR_:
        cur.h = cur_page_size.h - page_right_offset - one_true_inch;
        cur.v = cur_page_size.v - page_bottom_offset - one_true_inch;
        break;
    }
    /* The movement is actually done within the upright page coordinate system. */
    dvi_direction = dir_TL_;    /* only temporarily for this adjustment */
    synch_pos_with_cur();
    box_pos = pos;              /* keep track on page */
    /* Then switch to page box coordinate system; do |height(p)| movement. */
    dvi_direction = page_direction;
    cur.h = 0;
    cur.v = height(p);
    synch_pos_with_cur();
    /* Now we are at the point on the page where the origin of the page box should go. */
    temp_ptr = p;
    if (type(p) == vlist_node)
        vlist_out();
    else
        hlist_out();
    dvi_out(eop);
    incr(total_pages);
    cur_s = -1;
#ifdef IPC
    if (ipcon > 0) {
        if (dvi_limit == half_buf) {
            write_dvi(half_buf, dvi_buf_size - 1);
            flush_dvi();
            dvi_gone = dvi_gone + half_buf;
        }
        if (dvi_ptr > 0) {
            write_dvi(0, dvi_ptr - 1);
            flush_dvi();
            dvi_offset = dvi_offset + dvi_ptr;
            dvi_gone = dvi_gone + dvi_ptr;
        }
        dvi_ptr = 0;
        dvi_limit = dvi_buf_size;
        ipcpage(dvi_gone);
    }
#endif                          /* IPC */
  DONE:
    if ((tracing_output <= 0) && (post_callback_id == 0))
        print_char(']');
    dead_cycles = 0;
    /* Flush the box from memory, showing statistics if requested */
    if ((tracing_stats > 1) && (pre_callback_id == 0)) {
        tprint_nl("Memory usage before: ");
        print_int(var_used);
        print_char('&');
        print_int(dyn_used);
        print_char(';');
    }
    flush_node_list(p);
    if ((tracing_stats > 1) && (post_callback_id == 0)) {
        tprint(" after: ");
        print_int(var_used);
        print_char('&');
        print_int(dyn_used);
        print_ln();
    }
    if (post_callback_id > 0)
        ret = run_callback(post_callback_id, "->");
    /* Finish sheet {\sl Sync\TeX} information record */
    synctex_teehs();
}

/*
At the end of the program, we must finish things off by writing the
post\-amble. If |total_pages=0|, the \.{DVI} file was never opened.
If |total_pages>=65536|, the \.{DVI} file will lie. And if
|max_push>=65536|, the user deserves whatever chaos might ensue.
*/

void finish_dvi_file(int version, int revision)
{
    integer k;
    boolean res;
    integer callback_id = callback_defined(stop_run_callback);
    (void) version;
    (void) revision;
    while (cur_s > -1) {
        if (cur_s > 0) {
            dvi_out(pop);
        } else {
            dvi_out(eop);
            incr(total_pages);
        }
        decr(cur_s);
    }
    if (total_pages == 0) {
        if (callback_id == 0)
            tprint_nl("No pages of output.");
        else if (callback_id > 0)
            res = run_callback(callback_id, "->");
    } else {
        dvi_out(post);          /* beginning of the postamble */
        dvi_four(last_bop);
        last_bop = dvi_offset + dvi_ptr - 5;    /* |post| location */
        dvi_four(25400000);
        dvi_four(473628672);    /* conversion ratio for sp */
        prepare_mag();
        dvi_four(mag);          /* magnification factor */
        dvi_four(max_v);
        dvi_four(max_h);
        dvi_out(max_push / 256);
        dvi_out(max_push % 256);
        dvi_out((total_pages / 256) % 256);
        dvi_out(total_pages % 256);
        /* Output the font definitions for all fonts that were used */
        k = max_font_id();
        while (k > 0) {
            if (font_used(k)) {
                dvi_font_def(k);
            }
            decr(k);
        }

        dvi_out(post_post);
        dvi_four(last_bop);
        dvi_out(id_byte);
#ifndef IPC
        k = 4 + ((dvi_buf_size - dvi_ptr) % 4); /* the number of 223's */
#else
        k = 7 - ((3 + dvi_offset + dvi_ptr) % 4);       /* the number of 223's */
#endif

        while (k > 0) {
            dvi_out(223);
            decr(k);
        }
        /* Empty the last bytes out of |dvi_buf| */
        /* Here is how we clean out the buffer when \TeX\ is all through; |dvi_ptr|
           will be a multiple of~4. */
        if (dvi_limit == half_buf)
            write_dvi(half_buf, dvi_buf_size - 1);
        if (dvi_ptr > 0)
            write_dvi(0, dvi_ptr - 1);

        if (callback_id == 0) {
            tprint_nl("Output written on ");
            print_file_name(0, output_file_name, 0);
            tprint(" (");
            print_int(total_pages);
            tprint(" page");
            if (total_pages != 1)
                print_char('s');
            tprint(", ");
            print_int(dvi_offset + dvi_ptr);
            tprint(" bytes).");
        } else if (callback_id > 0) {
            res = run_callback(callback_id, "->");
        }
        b_close(dvi_file);
    }
}