/*    0.0 */       exec    // PredicateClean=false
/*   15   */          sgt r1.xyz_, -r_abs[0].xxxx, c254.xxxx
              +       setp_gt r0._, c26.x
/*    0.1 */ (!p0) jmp L27
/*    1.0 */       exec    // PredicateClean=false
/*   16   */          add r0.__zw, r0.xxxy, c0.xxxy
/*   17   */          tfetch2D r1.xyz_, r0.zw, tf0
/*   18   */          serialize
                      sgt r0.__z_, c26.xxxx, c253.yyyy
/*   19   */          mul r1.xyz_, r1.xyzz, c13.xyzz
              +       setp_ne r0._, r0.z
/*    1.1 */ (!p0) jmp L27
/*    2.0 */       exec    // PredicateClean=false
/*   20   */          add r0.__zw, r0.xxxy, c1.xxxy
/*   21   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   22   */          serialize
                      sgt r0.__z_, c26.xxxx, c252.wwww
/*   23   */          setp_ne r0._, r0.z
/*   24   */          mad r1.xyz_, r2.xyzz, c14.xyzz, r1.xyzz
/*    2.1 */ (!p0) jmp L27
/*    3.0 */       exec    // PredicateClean=false
/*   25   */          add r0.__zw, r0.xxxy, c2.xxxy
/*   26   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   27   */          serialize
                      sgt r0.__z_, c26.xxxx, c253.wwww
/*   28   */          setp_ne r0._, r0.z
/*   29   */          mad r1.xyz_, r2.xyzz, c15.xyzz, r1.xyzz
/*    3.1 */ (!p0) jmp L27
/*    4.0 */       exec    // PredicateClean=false
/*   30   */          add r0.__zw, r0.xxxy, c3.xxxy
/*   31   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   32   */          serialize
                      sgt r0.__z_, c26.xxxx, c254.wwww
/*   33   */          setp_ne r0._, r0.z
/*   34   */          mad r1.xyz_, r2.xyzz, c16.xyzz, r1.xyzz
/*    4.1 */ (!p0) jmp L27
/*    5.0 */       exec    // PredicateClean=false
/*   35   */          add r0.__zw, r0.xxxy, c4.xxxy
/*   36   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   37   */          serialize
                      sgt r0.__z_, c26.xxxx, c254.yyyy
/*   38   */          setp_ne r0._, r0.z
/*   39   */          mad r1.xyz_, r2.xyzz, c17.xyzz, r1.xyzz
/*    5.1 */ (!p0) jmp L27
/*    6.0 */       exec    // PredicateClean=false
/*   40   */          add r0.__zw, r0.xxxy, c5.xxxy
/*   41   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   42   */          serialize
                      sgt r0.__z_, c26.xxxx, c252.xxxx
/*   43   */          setp_ne r0._, r0.z
/*   44   */          mad r1.xyz_, r2.xyzz, c18.xyzz, r1.xyzz
/*    6.1 */ (!p0) jmp L27
/*    7.0 */       exec    // PredicateClean=false
/*   45   */          add r0.__zw, r0.xxxy, c6.xxxy
/*   46   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   47   */          serialize
                      sgt r0.__z_, c26.xxxx, c255.xxxx
/*   48   */          setp_ne r0._, r0.z
/*   49   */          mad r1.xyz_, r2.xyzz, c19.xyzz, r1.xyzz
/*    7.1 */ (!p0) jmp L27
/*    8.0 */       exec    // PredicateClean=false
/*   50   */          add r0.__zw, r0.xxxy, c7.xxxy
/*   51   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   52   */          serialize
                      sgt r0.__z_, c26.xxxx, c252.zzzz
/*   53   */          setp_ne r0._, r0.z
/*   54   */          mad r1.xyz_, r2.xyzz, c20.xyzz, r1.xyzz
/*    8.1 */ (!p0) jmp L27
/*    9.0 */       exec    // PredicateClean=false
/*   55   */          add r0.__zw, r0.xxxy, c8.xxxy
/*   56   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   57   */          serialize
                      sgt r0.__z_, c26.xxxx, c254.zzzz
/*   58   */          setp_ne r0._, r0.z
/*   59   */          mad r1.xyz_, r2.xyzz, c21.xyzz, r1.xyzz
/*    9.1 */ (!p0) jmp L27
/*   10.0 */       exec    // PredicateClean=false
/*   60   */          add r0.__zw, r0.xxxy, c9.xxxy
/*   61   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   62   */          serialize
                      sgt r0.__z_, c26.xxxx, c252.yyyy
/*   63   */          setp_ne r0._, r0.z
/*   64   */          mad r1.xyz_, r2.xyzz, c22.xyzz, r1.xyzz
/*   10.1 */ (!p0) jmp L27
/*   11.0 */       exec    // PredicateClean=false
/*   65   */          add r0.__zw, r0.xxxy, c10.xxxy
/*   66   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   67   */          serialize
                      sgt r0.__z_, c26.xxxx, c253.xxxx
/*   68   */          setp_ne r0._, r0.z
/*   69   */          mad r1.xyz_, r2.xyzz, c23.xyzz, r1.xyzz
/*   11.1 */ (!p0) jmp L27
/*   12.0 */       exec    // PredicateClean=false
/*   70   */          add r0.__zw, r0.xxxy, c11.xxxy
/*   71   */          tfetch2D r2.xyz_, r0.zw, tf0
/*   72   */          serialize
                      sgt r0.__z_, c26.xxxx, c253.zzzz
/*   73   */          setp_ne r0._, r0.z
/*   74   */          mad r1.xyz_, r2.xyzz, c24.xyzz, r1.xyzz
/*   12.1 */ (!p0) jmp L27
/*   13.0 */       exec
/*   75   */          add r0.xy__, r0.xyyy, c12.xyyy
/*   76   */          tfetch2D r0.xyz_, r0.xy, tf0
/*   77   */          serialize
                      mad r1.xyz_, r0.xyzz, c25.xyzz, r1.xyzz
                label L27
/*   13.1 */       alloc colors
/*   14.0 */       exece
/*   78   */          max oC0.xyz1, r1.xyzz, r1.xyzz
/*   14.1 */       cnop
