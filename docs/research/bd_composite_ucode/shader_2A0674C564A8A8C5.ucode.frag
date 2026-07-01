/*    0.0 */       exec
/*    2   */          tfetch2D r0.xyz_, r0.xy, tf0
/*    0.1 */       alloc colors
/*    1.0 */       exec
/*    3   */          add r1.xyz_, r0.xyzz, -c27.xxxx
/*    4   */          min r0.x_zw, r0.xyyz, c254.xxxx
              +       maxs r0._, c27.yy
/*    5   */          max r1._yzw, r1.xxyz, c255.wwww
              +       muls_prev r0._y__, c254.z
/*    6   */          mul r2.__z_, r0.yyyy, r1.yyyy
              +       mulsc r1.x___, c254.y, r0.x
/*    7   */          mul r2.xy__, r1.zwww, c27.yyyy
              +       mulsc r1._y__, c254.y, r0.z
/*    8   */          dp3 r0._y__, r2.yxzz, c255.xyzz
              +       mulsc r1.__z_, c254.y, r0.w
/*    1.1 */       exece
/*    9   */          mul oC0.xyz1, r1.xyzz, r0.yyyy
