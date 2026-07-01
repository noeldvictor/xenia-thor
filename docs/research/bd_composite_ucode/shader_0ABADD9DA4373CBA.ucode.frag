/*    0.0 */       exec    // PredicateClean=false
/*   11   */          sgt r2, -r_abs[0].xxxx, c254.xxxx
              +       setp_gt r0._, c26.x
/*    0.1 */ (!p0) jmp L19
/*    1.0 */       exec    // PredicateClean=false
/*   12   */          tfetch2D r1, r0.xy, tf0
/*   13   */          serialize
                      sgt r0.__z_, c26.xxxx, c254.wwww
/*   14   */          mul r1, r1.wyxz, c13.wyxz
/*   15   */          max_sat r2, r1.zywx, r1.zywx
              +       setp_ne r0._, r0.z
/*    1.1 */ (!p0) jmp L19
/*    2.0 */       exec    // PredicateClean=false
/*   16   */          tfetch2D r2, r0.xy, tf1
/*   17   */          serialize
                      sgt r0.__z_, c26.xxxx, c253.wwww
/*   18   */          mad r1, r2.wyxz, c14.wyxz, r1
/*   19   */          max_sat r2, r1.zywx, r1.zywx
              +       setp_ne r0._, r0.z
/*    2.1 */ (!p0) jmp L19
/*    3.0 */       exec    // PredicateClean=false
/*   20   */          tfetch2D r2, r0.xy, tf2
/*   21   */          serialize
                      sgt r0.__z_, c26.xxxx, c253.yyyy
/*   22   */          mad r1, r2.wyxz, c15.wyxz, r1
/*   23   */          max_sat r2, r1.zywx, r1.zywx
              +       setp_ne r0._, r0.z
/*    3.1 */ (!p0) jmp L19
/*    4.0 */       exec    // PredicateClean=false
/*   24   */          tfetch2D r2, r0.xy, tf3
/*   25   */          serialize
                      sgt r0.__z_, c26.xxxx, c254.zzzz
/*   26   */          mad r1, r2.wyxz, c16.wyxz, r1
/*   27   */          max_sat r2, r1.zywx, r1.zywx
              +       setp_ne r0._, r0.z
/*    4.1 */ (!p0) jmp L19
/*    5.0 */       exec    // PredicateClean=false
/*   28   */          tfetch2D r2, r0.xy, tf4
/*   29   */          serialize
                      sgt r0.__z_, c26.xxxx, c254.yyyy
/*   30   */          mad r1, r2.wyxz, c17.wyxz, r1
/*   31   */          max_sat r2, r1.zywx, r1.zywx
              +       setp_ne r0._, r0.z
/*    5.1 */ (!p0) jmp L19
/*    6.0 */       exec    // PredicateClean=false
/*   32   */          tfetch2D r2, r0.xy, tf5
/*   33   */          serialize
                      sgt r0.__z_, c26.xxxx, c253.xxxx
/*   34   */          mad r1, r2.wyxz, c18.wyxz, r1
/*   35   */          max_sat r2, r1.zywx, r1.zywx
              +       setp_ne r0._, r0.z
/*    6.1 */ (!p0) jmp L19
/*    7.0 */       exec    // PredicateClean=false
/*   36   */          tfetch2D r2, r0.xy, tf6
/*   37   */          serialize
                      sgt r0.__z_, c26.xxxx, c255.xxxx
/*   38   */          mad r1, r2.wyxz, c19.wyxz, r1
/*   39   */          max_sat r2, r1.zywx, r1.zywx
              +       setp_ne r0._, r0.z
/*    7.1 */ (!p0) jmp L19
/*    8.0 */       exec    // PredicateClean=false
/*   40   */          tfetch2D r2, r0.xy, tf7
/*   41   */          serialize
                      sgt r0.__z_, c26.xxxx, c253.zzzz
/*   42   */          mad r1, r2.zwyx, c20.zwyx, r1.wxyz
/*   43   */          max_sat r2, r1.wzxy, r1.wzxy
              +       setp_ne r0._, r0.z
/*    8.1 */ (!p0) jmp L19
/*    9.0 */       exec
/*   44   */          tfetch2D r0, r0.xy, tf8
/*   45   */          serialize
                      mad_sat r2, r0, c21, r1.wzxy
                label L19
/*    9.1 */       alloc colors
/*   10.0 */       exece
/*   46   */          max oC0, r2, r2
/*   10.1 */       cnop
