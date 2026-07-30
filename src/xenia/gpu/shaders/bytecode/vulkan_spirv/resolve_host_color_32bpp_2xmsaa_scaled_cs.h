// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.3
; Generator: Google Shaderc over Glslang; 10
; Bound: 7501
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %main "main" %gl_GlobalInvocationID
               OpExecutionMode %main LocalSize 8 8 1
               OpSource GLSL 460
               OpSourceExtension "GL_EXT_control_flow_attributes"
               OpSourceExtension "GL_EXT_samplerless_texture_functions"
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %push_const_block_xe "push_const_block_xe"
               OpMemberName %push_const_block_xe 0 "xe_resolve_edram_info"
               OpMemberName %push_const_block_xe 1 "xe_resolve_coordinate_info"
               OpMemberName %push_const_block_xe 2 "xe_resolve_dest_info"
               OpMemberName %push_const_block_xe 3 "xe_resolve_dest_coordinate_info"
               OpName %push_consts_xe "push_consts_xe"
               OpName %xe_resolve_host_color_xe_block "xe_resolve_host_color_xe_block"
               OpMemberName %xe_resolve_host_color_xe_block 0 "xe_resolve_host_color_dispatch_offset"
               OpMemberName %xe_resolve_host_color_xe_block 1 "xe_resolve_host_color_dump_base"
               OpMemberName %xe_resolve_host_color_xe_block 2 "xe_resolve_host_color_dump_pitch_tiles"
               OpMemberName %xe_resolve_host_color_xe_block 3 "xe_resolve_host_color_source_base_tiles"
               OpMemberName %xe_resolve_host_color_xe_block 4 "xe_resolve_host_color_source_pitch_tiles"
               OpMemberName %xe_resolve_host_color_xe_block 5 "xe_resolve_host_color_thread_count_x"
               OpMemberName %xe_resolve_host_color_xe_block 6 "xe_resolve_host_color_thread_count_y"
               OpMemberName %xe_resolve_host_color_xe_block 7 "xe_resolve_host_color_height_scaled"
               OpMemberName %xe_resolve_host_color_xe_block 8 "xe_resolve_host_color_msaa_2x_sample_0"
               OpMemberName %xe_resolve_host_color_xe_block 9 "xe_resolve_host_color_msaa_2x_sample_1"
               OpMemberName %xe_resolve_host_color_xe_block 10 "xe_resolve_host_color_flags"
               OpName %xe_resolve_host_color "xe_resolve_host_color"
               OpName %xe_resolve_host_color_source "xe_resolve_host_color_source"
               OpName %xe_resolve_dest_xe_block "xe_resolve_dest_xe_block"
               OpMemberName %xe_resolve_dest_xe_block 0 "data"
               OpName %xe_resolve_dest "xe_resolve_dest"
               OpName %gl_GlobalInvocationID "gl_GlobalInvocationID"
               OpMemberDecorate %push_const_block_xe 0 Offset 0
               OpMemberDecorate %push_const_block_xe 1 Offset 4
               OpMemberDecorate %push_const_block_xe 2 Offset 8
               OpMemberDecorate %push_const_block_xe 3 Offset 12
               OpDecorate %push_const_block_xe Block
               OpMemberDecorate %xe_resolve_host_color_xe_block 0 Offset 0
               OpMemberDecorate %xe_resolve_host_color_xe_block 1 Offset 4
               OpMemberDecorate %xe_resolve_host_color_xe_block 2 Offset 8
               OpMemberDecorate %xe_resolve_host_color_xe_block 3 Offset 12
               OpMemberDecorate %xe_resolve_host_color_xe_block 4 Offset 16
               OpMemberDecorate %xe_resolve_host_color_xe_block 5 Offset 20
               OpMemberDecorate %xe_resolve_host_color_xe_block 6 Offset 24
               OpMemberDecorate %xe_resolve_host_color_xe_block 7 Offset 28
               OpMemberDecorate %xe_resolve_host_color_xe_block 8 Offset 32
               OpMemberDecorate %xe_resolve_host_color_xe_block 9 Offset 36
               OpMemberDecorate %xe_resolve_host_color_xe_block 10 Offset 40
               OpDecorate %xe_resolve_host_color_xe_block Block
               OpDecorate %xe_resolve_host_color DescriptorSet 0
               OpDecorate %xe_resolve_host_color Binding 1
               OpDecorate %xe_resolve_host_color_source DescriptorSet 2
               OpDecorate %xe_resolve_host_color_source Binding 0
               OpDecorate %_runtimearr_v4uint ArrayStride 16
               OpMemberDecorate %xe_resolve_dest_xe_block 0 NonReadable
               OpMemberDecorate %xe_resolve_dest_xe_block 0 Offset 0
               OpDecorate %xe_resolve_dest_xe_block Block
               OpDecorate %xe_resolve_dest DescriptorSet 1
               OpDecorate %xe_resolve_dest Binding 0
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
     %v4uint = OpTypeVector %uint 4
        %int = OpTypeInt 32 1
      %v2int = OpTypeVector %int 2
      %v3int = OpTypeVector %int 3
       %bool = OpTypeBool
    %float_0 = OpConstant %float 0
    %float_1 = OpConstant %float 1
     %uint_1 = OpConstant %uint 1
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
     %uint_8 = OpConstant %uint 8
%uint_4278255360 = OpConstant %uint 4278255360
     %uint_3 = OpConstant %uint 3
    %uint_16 = OpConstant %uint 16
  %float_255 = OpConstant %float 255
  %float_0_5 = OpConstant %float 0.5
     %uint_0 = OpConstant %uint 0
      %int_8 = OpConstant %int 8
     %int_16 = OpConstant %int 16
     %int_24 = OpConstant %int 24
 %float_1023 = OpConstant %float 1023
    %float_3 = OpConstant %float 3
        %297 = OpConstantComposite %v4float %float_1023 %float_1023 %float_1023 %float_3
     %int_10 = OpConstant %int 10
     %int_20 = OpConstant %int 20
     %int_30 = OpConstant %int 30
      %int_4 = OpConstant %int 4
      %int_6 = OpConstant %int 6
     %int_11 = OpConstant %int 11
     %int_15 = OpConstant %int 15
      %int_1 = OpConstant %int 1
      %int_5 = OpConstant %int 5
      %int_7 = OpConstant %int 7
     %int_12 = OpConstant %int 12
      %int_3 = OpConstant %int 3
      %int_2 = OpConstant %int 2
     %uint_5 = OpConstant %uint 5
     %uint_4 = OpConstant %uint 4
      %int_0 = OpConstant %int 0
%push_const_block_xe = OpTypeStruct %uint %uint %uint %uint
%_ptr_PushConstant_push_const_block_xe = OpTypePointer PushConstant %push_const_block_xe
%push_consts_xe = OpVariable %_ptr_PushConstant_push_const_block_xe PushConstant
%_ptr_PushConstant_uint = OpTypePointer PushConstant %uint
  %uint_1023 = OpConstant %uint 1023
    %uint_10 = OpConstant %uint 10
  %uint_2047 = OpConstant %uint 2047
    %uint_24 = OpConstant %uint 24
    %uint_15 = OpConstant %uint 15
    %uint_28 = OpConstant %uint 28
    %uint_19 = OpConstant %uint 19
        %568 = OpConstantComposite %v2uint %uint_16 %uint_19
     %uint_7 = OpConstant %uint 7
%uint_536870912 = OpConstant %uint 536870912
        %592 = OpConstantComposite %v2uint %uint_0 %uint_4
        %596 = OpConstantComposite %v2uint %uint_4 %uint_1
      %int_9 = OpConstant %int 9
%uint_16777216 = OpConstant %uint 16777216
    %uint_20 = OpConstant %uint 20
        %669 = OpConstantComposite %v2uint %uint_20 %uint_24
     %v3uint = OpTypeVector %uint 3
   %uint_255 = OpConstant %uint 255
%uint_3222273024 = OpConstant %uint 3222273024
    %uint_80 = OpConstant %uint 80
   %float_n1 = OpConstant %float -1
 %float_n0_5 = OpConstant %float -0.5
%float_32767 = OpConstant %float 32767
 %uint_65535 = OpConstant %uint 65535
%uint_1048576000 = OpConstant %uint 1048576000
    %uint_23 = OpConstant %uint 23
   %uint_125 = OpConstant %uint 125
%uint_8388607 = OpConstant %uint 8388607
%uint_8388608 = OpConstant %uint 8388608
%uint_3254779904 = OpConstant %uint 3254779904
 %uint_32767 = OpConstant %uint 32767
%float_31_875 = OpConstant %float 31.875
    %uint_30 = OpConstant %uint 30
    %v2float = OpTypeVector %float 2
%xe_resolve_host_color_xe_block = OpTypeStruct %uint %uint %uint %uint %uint %uint %uint %uint %uint %uint %uint
%_ptr_Uniform_xe_resolve_host_color_xe_block = OpTypePointer Uniform %xe_resolve_host_color_xe_block
%xe_resolve_host_color = OpVariable %_ptr_Uniform_xe_resolve_host_color_xe_block Uniform
%_ptr_Uniform_uint = OpTypePointer Uniform %uint
       %1065 = OpConstantComposite %v2uint %uint_1 %uint_0
       %1096 = OpTypeImage %float 2D 0 0 1 1 Unknown
%_ptr_UniformConstant_1096 = OpTypePointer UniformConstant %1096
%xe_resolve_host_color_source = OpVariable %_ptr_UniformConstant_1096 UniformConstant
      %false = OpConstantFalse %bool
       %true = OpConstantTrue %bool
       %1264 = OpConstantComposite %v2uint %uint_0 %uint_1
     %uint_6 = OpConstant %uint 6
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%xe_resolve_dest_xe_block = OpTypeStruct %_runtimearr_v4uint
%_ptr_StorageBuffer_xe_resolve_dest_xe_block = OpTypePointer StorageBuffer %xe_resolve_dest_xe_block
%xe_resolve_dest = OpVariable %_ptr_StorageBuffer_xe_resolve_dest_xe_block StorageBuffer
%_ptr_StorageBuffer_v4uint = OpTypePointer StorageBuffer %v4uint
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %7182 = OpUndef %v2uint
       %7474 = OpConstantComposite %v2uint %uint_7 %uint_7
       %7475 = OpConstantComposite %v2uint %uint_1 %uint_1
       %7476 = OpConstantComposite %v2uint %uint_0 %uint_0
       %7477 = OpConstantComposite %v2uint %uint_3 %uint_3
       %7478 = OpConstantComposite %v2uint %uint_15 %uint_15
       %7479 = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0
       %7480 = OpConstantComposite %v4float %float_1 %float_1 %float_1 %float_1
       %7481 = OpConstantComposite %v4float %float_0_5 %float_0_5 %float_0_5 %float_0_5
       %7482 = OpConstantComposite %v4uint %uint_4278255360 %uint_4278255360 %uint_4278255360 %uint_4278255360
       %7483 = OpConstantComposite %v4uint %uint_255 %uint_255 %uint_255 %uint_255
       %7484 = OpConstantComposite %v4uint %uint_16 %uint_16 %uint_16 %uint_16
       %7485 = OpConstantComposite %v4uint %uint_3222273024 %uint_3222273024 %uint_3222273024 %uint_3222273024
       %7486 = OpConstantComposite %v4uint %uint_1023 %uint_1023 %uint_1023 %uint_1023
       %7487 = OpConstantComposite %v4uint %uint_20 %uint_20 %uint_20 %uint_20
       %7488 = OpConstantComposite %v4uint %uint_16711935 %uint_16711935 %uint_16711935 %uint_16711935
       %7489 = OpConstantComposite %v4uint %uint_8 %uint_8 %uint_8 %uint_8
       %main = OpFunction %void None %3
          %5 = OpLabel
       %1512 = OpLoad %v3uint %gl_GlobalInvocationID
               OpSelectionMerge %1720 None
               OpSwitch %uint_0 %1608
       %1608 = OpLabel
       %1733 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_0
       %1734 = OpLoad %uint %1733
       %1735 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_1
       %1736 = OpLoad %uint %1735
       %1753 = OpShiftRightLogical %uint %1734 %uint_24
       %1754 = OpBitwiseAnd %uint %1753 %uint_15
       %1861 = OpCompositeConstruct %v2uint %1736 %1736
       %1762 = OpShiftRightLogical %v2uint %1861 %568
       %1764 = OpBitwiseAnd %v2uint %1762 %7474
       %1767 = OpBitwiseAnd %uint %1734 %uint_536870912
       %1768 = OpINotEqual %bool %1767 %uint_0
               OpSelectionMerge %1778 None
               OpBranchConditional %1768 %1769 %1775
       %1775 = OpLabel
               OpBranch %1778
       %1769 = OpLabel
       %1773 = OpShiftRightLogical %v2uint %1764 %7475
               OpBranch %1778
       %1778 = OpLabel
       %7177 = OpPhi %v2uint %1773 %1769 %7476 %1775
       %1781 = OpShiftRightLogical %v2uint %1861 %592
       %1783 = OpShiftLeftLogical %v2uint %7475 %596
       %1785 = OpISub %v2uint %1783 %7475
       %1786 = OpBitwiseAnd %v2uint %1781 %1785
       %1788 = OpShiftLeftLogical %v2uint %1786 %7477
       %1791 = OpIMul %v2uint %1788 %1764
       %1794 = OpShiftRightLogical %uint %1736 %uint_5
       %1795 = OpBitwiseAnd %uint %1794 %uint_2047
       %1797 = OpCompositeExtract %uint %1764 0
       %1798 = OpIMul %uint %1795 %1797
       %1800 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_2
       %1801 = OpLoad %uint %1800
       %1802 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_3
       %1803 = OpLoad %uint %1802
       %1805 = OpBitwiseAnd %uint %1801 %uint_7
       %1808 = OpBitwiseAnd %uint %1801 %uint_8
       %1809 = OpINotEqual %bool %1808 %uint_0
       %1812 = OpShiftRightLogical %uint %1801 %uint_4
       %1813 = OpBitwiseAnd %uint %1812 %uint_7
       %1829 = OpBitwiseAnd %uint %1801 %uint_16777216
       %1830 = OpINotEqual %bool %1829 %uint_0
       %1833 = OpBitwiseAnd %uint %1803 %uint_1023
       %1836 = OpShiftRightLogical %uint %1803 %uint_10
       %1837 = OpBitwiseAnd %uint %1836 %uint_1023
       %1838 = OpShiftLeftLogical %uint %1837 %int_1
       %1881 = OpCompositeConstruct %v2uint %1803 %1803
       %1842 = OpShiftRightLogical %v2uint %1881 %669
       %1844 = OpBitwiseAnd %v2uint %1842 %7478
       %1846 = OpShiftLeftLogical %v2uint %1844 %7477
       %1849 = OpIMul %v2uint %1846 %1764
       %1852 = OpShiftRightLogical %uint %1803 %uint_28
       %1853 = OpBitwiseAnd %uint %1852 %uint_7
               OpSelectionMerge %2013 None
               OpSwitch %uint_0 %1902
       %1902 = OpLabel
       %1904 = OpCompositeExtract %uint %1512 0
       %1905 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_5
       %1906 = OpLoad %uint %1905
       %1907 = OpUGreaterThanEqual %bool %1904 %1906
       %1908 = OpLogicalNot %bool %1907
               OpSelectionMerge %1915 None
               OpBranchConditional %1908 %1909 %1915
       %1909 = OpLabel
       %1911 = OpCompositeExtract %uint %1512 1
       %1912 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_6
       %1913 = OpLoad %uint %1912
       %1914 = OpUGreaterThanEqual %bool %1911 %1913
               OpBranch %1915
       %1915 = OpLabel
       %1916 = OpPhi %bool %1907 %1902 %1914 %1909
               OpSelectionMerge %1918 None
               OpBranchConditional %1916 %1917 %1918
       %1917 = OpLabel
               OpBranch %2013
       %1918 = OpLabel
       %2026 = OpIMul %uint %uint_80 %1797
       %2036 = OpCompositeExtract %uint %1764 1
       %2037 = OpIMul %uint %uint_16 %2036
       %2032 = OpShiftRightLogical %uint %2037 %uint_1
       %1927 = OpIMul %uint %1904 %uint_8
       %1929 = OpCompositeExtract %uint %1512 1
       %1932 = OpUDiv %uint %1927 %2026
       %1935 = OpUDiv %uint %1929 %2032
       %1939 = OpIMul %uint %1932 %2026
       %1940 = OpISub %uint %1927 %1939
       %1944 = OpIMul %uint %1935 %2032
       %1945 = OpISub %uint %1929 %1944
       %1946 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_0
       %1947 = OpLoad %uint %1946
       %1949 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_2
       %1950 = OpLoad %uint %1949
       %1951 = OpIMul %uint %1935 %1950
       %1952 = OpIAdd %uint %1947 %1951
       %1954 = OpIAdd %uint %1952 %1932
       %1959 = OpUDiv %uint %1954 %1950
       %1963 = OpIMul %uint %1959 %1950
       %1964 = OpISub %uint %1954 %1963
       %1967 = OpIMul %uint %1964 %2026
       %1969 = OpIAdd %uint %1967 %1940
       %1972 = OpIMul %uint %1959 %2032
       %1974 = OpIAdd %uint %1972 %1945
       %1975 = OpCompositeConstruct %v2uint %1969 %1974
       %1979 = OpCompositeExtract %uint %1791 0
       %1980 = OpULessThan %bool %1969 %1979
       %1981 = OpLogicalNot %bool %1980
               OpSelectionMerge %1988 None
               OpBranchConditional %1981 %1982 %1988
       %1982 = OpLabel
       %1986 = OpCompositeExtract %uint %1791 1
       %1987 = OpULessThan %bool %1974 %1986
               OpBranch %1988
       %1988 = OpLabel
       %1989 = OpPhi %bool %1980 %1918 %1987 %1982
               OpSelectionMerge %1991 None
               OpBranchConditional %1989 %1990 %1991
       %1990 = OpLabel
               OpBranch %2013
       %1991 = OpLabel
       %1995 = OpISub %v2uint %1975 %1791
       %1997 = OpCompositeExtract %uint %1995 0
       %2000 = OpShiftLeftLogical %uint %1798 %uint_3
       %2001 = OpUGreaterThanEqual %bool %1997 %2000
       %2002 = OpLogicalNot %bool %2001
               OpSelectionMerge %2009 None
               OpBranchConditional %2002 %2003 %2009
       %2003 = OpLabel
       %2005 = OpCompositeExtract %uint %1995 1
       %2006 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_7
       %2007 = OpLoad %uint %2006
       %2008 = OpUGreaterThanEqual %bool %2005 %2007
               OpBranch %2009
       %2009 = OpLabel
       %2010 = OpPhi %bool %2001 %1991 %2008 %2003
               OpSelectionMerge %2012 None
               OpBranchConditional %2010 %2011 %2012
       %2011 = OpLabel
               OpBranch %2013
       %2012 = OpLabel
               OpBranch %2013
       %2013 = OpLabel
       %7179 = OpPhi %v2uint %7182 %1917 %7182 %1990 %1995 %2011 %1995 %2012
       %7178 = OpPhi %bool %false %1917 %false %1990 %false %2011 %true %2012
       %1614 = OpLogicalNot %bool %7178
               OpSelectionMerge %1616 None
               OpBranchConditional %1614 %1615 %1616
       %1615 = OpLabel
               OpBranch %1720
       %1616 = OpLabel
       %2056 = OpULessThanEqual %bool %1853 %uint_3
               OpSelectionMerge %2065 None
               OpBranchConditional %2056 %2057 %2059
       %2059 = OpLabel
       %2061 = OpIEqual %bool %1853 %uint_5
       %7493 = OpSelect %uint %2061 %uint_2 %uint_0
               OpBranch %2065
       %2057 = OpLabel
               OpBranch %2065
       %2065 = OpLabel
       %7185 = OpPhi %uint %1853 %2057 %7493 %2059
       %2072 = OpCompositeExtract %uint %7179 0
       %2076 = OpCompositeExtract %uint %7179 1
       %2078 = OpCompositeExtract %uint %7177 1
       %2079 = OpExtInst %uint %1 UMax %2076 %2078
       %2080 = OpCompositeConstruct %v2uint %2072 %2079
       %2083 = OpIAdd %v2uint %2080 %1791
       %2085 = OpShiftLeftLogical %v2uint %2083 %1264
       %2101 = OpCompositeConstruct %v2uint %7185 %7185
       %2094 = OpShiftRightLogical %v2uint %2101 %1065
       %2096 = OpBitwiseAnd %v2uint %2094 %7475
       %2088 = OpIAdd %v2uint %2085 %2096
       %2209 = OpIMul %uint %uint_80 %1797
       %2213 = OpCompositeExtract %uint %1764 1
       %2214 = OpIMul %uint %uint_16 %2213
       %2151 = OpCompositeExtract %uint %2088 0
       %2153 = OpUDiv %uint %2151 %2209
       %2155 = OpCompositeExtract %uint %2088 1
       %2157 = OpUDiv %uint %2155 %2214
       %2162 = OpIMul %uint %2153 %2209
       %2163 = OpISub %uint %2151 %2162
       %2168 = OpIMul %uint %2157 %2214
       %2169 = OpISub %uint %2155 %2168
       %2171 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_2
       %2172 = OpLoad %uint %2171
       %2173 = OpIMul %uint %2157 %2172
       %2175 = OpIAdd %uint %2173 %2153
       %2176 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_1
       %2177 = OpLoad %uint %2176
       %2179 = OpIAdd %uint %2177 %2175
       %2181 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_3
       %2182 = OpLoad %uint %2181
       %2183 = OpISub %uint %2179 %2182
       %2184 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_4
       %2185 = OpLoad %uint %2184
       %2188 = OpUDiv %uint %2183 %2185
       %2192 = OpIMul %uint %2188 %2185
       %2193 = OpISub %uint %2183 %2192
       %2196 = OpIMul %uint %2193 %2209
       %2198 = OpIAdd %uint %2196 %2163
       %2201 = OpIMul %uint %2188 %2214
       %2203 = OpIAdd %uint %2201 %2169
       %2219 = OpBitwiseAnd %uint %2203 %uint_1
       %2220 = OpINotEqual %bool %2219 %uint_0
               OpSelectionMerge %2227 None
               OpBranchConditional %2220 %2221 %2224
       %2224 = OpLabel
       %2225 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2226 = OpLoad %uint %2225
               OpBranch %2227
       %2221 = OpLabel
       %2222 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2223 = OpLoad %uint %2222
               OpBranch %2227
       %2227 = OpLabel
       %7187 = OpPhi %uint %2223 %2221 %2226 %2224
       %2115 = OpLoad %1096 %xe_resolve_host_color_source
       %2118 = OpBitcast %int %2198
       %2121 = OpShiftRightLogical %uint %2203 %uint_1
       %2122 = OpBitcast %int %2121
       %2126 = OpCompositeConstruct %v2int %2118 %2122
       %2128 = OpBitcast %int %7187
       %2129 = OpImageFetch %v4float %2115 %2126 Sample %2128
               OpSelectionMerge %2286 None
               OpSwitch %1754 %2244 0 %2248 1 %2248 2 %2251 10 %2251 3 %2254 12 %2254 4 %2273 6 %2282
       %2282 = OpLabel
       %2284 = OpVectorShuffle %v2float %2129 %2129 0 1
       %2285 = OpExtInst %uint %1 PackHalf2x16 %2284
               OpBranch %2286
       %2273 = OpLabel
       %2275 = OpCompositeExtract %float %2129 0
       %2539 = OpExtInst %float %1 FMax %2275 %float_n1
       %2540 = OpExtInst %float %1 FMin %2539 %float_1
       %2542 = OpFOrdGreaterThanEqual %bool %2540 %float_0
       %2543 = OpSelect %float %2542 %float_0_5 %float_n0_5
       %2547 = OpExtInst %float %1 Fma %2540 %float_32767 %2543
       %2548 = OpConvertFToS %int %2547
       %2549 = OpBitcast %uint %2548
       %2550 = OpBitwiseAnd %uint %2549 %uint_65535
       %2278 = OpCompositeExtract %float %2129 1
       %2556 = OpExtInst %float %1 FMax %2278 %float_n1
       %2557 = OpExtInst %float %1 FMin %2556 %float_1
       %2559 = OpFOrdGreaterThanEqual %bool %2557 %float_0
       %2560 = OpSelect %float %2559 %float_0_5 %float_n0_5
       %2564 = OpExtInst %float %1 Fma %2557 %float_32767 %2560
       %2565 = OpConvertFToS %int %2564
       %2566 = OpBitcast %uint %2565
       %2567 = OpBitwiseAnd %uint %2566 %uint_65535
       %2280 = OpShiftLeftLogical %uint %2567 %uint_16
       %2281 = OpBitwiseOr %uint %2550 %2280
               OpBranch %2286
       %2254 = OpLabel
       %2256 = OpCompositeExtract %float %2129 0
       %2387 = OpExtInst %float %1 FMax %2256 %float_0
       %2388 = OpExtInst %float %1 FMin %2387 %float_31_875
       %2400 = OpBitcast %uint %2388
       %2402 = OpULessThan %bool %2400 %uint_1048576000
               OpSelectionMerge %2418 None
               OpBranchConditional %2402 %2403 %2415
       %2415 = OpLabel
       %2417 = OpIAdd %uint %2400 %uint_3254779904
               OpBranch %2418
       %2403 = OpLabel
       %2405 = OpShiftRightLogical %uint %2400 %uint_23
       %2407 = OpISub %uint %uint_125 %2405
       %2408 = OpExtInst %uint %1 UMin %2407 %uint_24
       %2410 = OpBitwiseAnd %uint %2400 %uint_8388607
       %2411 = OpBitwiseOr %uint %2410 %uint_8388608
       %2414 = OpShiftRightLogical %uint %2411 %2408
               OpBranch %2418
       %2418 = OpLabel
       %7188 = OpPhi %uint %2414 %2403 %2417 %2415
       %2420 = OpShiftRightLogical %uint %7188 %uint_16
       %2421 = OpBitwiseAnd %uint %2420 %uint_1
       %2423 = OpIAdd %uint %7188 %uint_32767
       %2425 = OpIAdd %uint %2423 %2421
       %2427 = OpShiftRightLogical %uint %2425 %uint_16
       %2428 = OpBitwiseAnd %uint %2427 %uint_1023
       %2259 = OpCompositeExtract %float %2129 1
       %2433 = OpExtInst %float %1 FMax %2259 %float_0
       %2434 = OpExtInst %float %1 FMin %2433 %float_31_875
       %2446 = OpBitcast %uint %2434
       %2448 = OpULessThan %bool %2446 %uint_1048576000
               OpSelectionMerge %2464 None
               OpBranchConditional %2448 %2449 %2461
       %2461 = OpLabel
       %2463 = OpIAdd %uint %2446 %uint_3254779904
               OpBranch %2464
       %2449 = OpLabel
       %2451 = OpShiftRightLogical %uint %2446 %uint_23
       %2453 = OpISub %uint %uint_125 %2451
       %2454 = OpExtInst %uint %1 UMin %2453 %uint_24
       %2456 = OpBitwiseAnd %uint %2446 %uint_8388607
       %2457 = OpBitwiseOr %uint %2456 %uint_8388608
       %2460 = OpShiftRightLogical %uint %2457 %2454
               OpBranch %2464
       %2464 = OpLabel
       %7189 = OpPhi %uint %2460 %2449 %2463 %2461
       %2466 = OpShiftRightLogical %uint %7189 %uint_16
       %2467 = OpBitwiseAnd %uint %2466 %uint_1
       %2469 = OpIAdd %uint %7189 %uint_32767
       %2471 = OpIAdd %uint %2469 %2467
       %2473 = OpShiftRightLogical %uint %2471 %uint_16
       %2474 = OpBitwiseAnd %uint %2473 %uint_1023
       %2261 = OpShiftLeftLogical %uint %2474 %uint_10
       %2262 = OpBitwiseOr %uint %2428 %2261
       %2264 = OpCompositeExtract %float %2129 2
       %2479 = OpExtInst %float %1 FMax %2264 %float_0
       %2480 = OpExtInst %float %1 FMin %2479 %float_31_875
       %2492 = OpBitcast %uint %2480
       %2494 = OpULessThan %bool %2492 %uint_1048576000
               OpSelectionMerge %2510 None
               OpBranchConditional %2494 %2495 %2507
       %2507 = OpLabel
       %2509 = OpIAdd %uint %2492 %uint_3254779904
               OpBranch %2510
       %2495 = OpLabel
       %2497 = OpShiftRightLogical %uint %2492 %uint_23
       %2499 = OpISub %uint %uint_125 %2497
       %2500 = OpExtInst %uint %1 UMin %2499 %uint_24
       %2502 = OpBitwiseAnd %uint %2492 %uint_8388607
       %2503 = OpBitwiseOr %uint %2502 %uint_8388608
       %2506 = OpShiftRightLogical %uint %2503 %2500
               OpBranch %2510
       %2510 = OpLabel
       %7190 = OpPhi %uint %2506 %2495 %2509 %2507
       %2512 = OpShiftRightLogical %uint %7190 %uint_16
       %2513 = OpBitwiseAnd %uint %2512 %uint_1
       %2515 = OpIAdd %uint %7190 %uint_32767
       %2517 = OpIAdd %uint %2515 %2513
       %2519 = OpShiftRightLogical %uint %2517 %uint_16
       %2520 = OpBitwiseAnd %uint %2519 %uint_1023
       %2266 = OpShiftLeftLogical %uint %2520 %uint_20
       %2267 = OpBitwiseOr %uint %2262 %2266
       %2269 = OpCompositeExtract %float %2129 3
       %2533 = OpExtInst %float %1 FClamp %2269 %float_0 %float_1
       %2528 = OpExtInst %float %1 Fma %2533 %float_3 %float_0_5
       %2529 = OpConvertFToU %uint %2528
       %2271 = OpShiftLeftLogical %uint %2529 %uint_30
       %2272 = OpBitwiseOr %uint %2267 %2271
               OpBranch %2286
       %2251 = OpLabel
       %2368 = OpExtInst %v4float %1 FClamp %2129 %7479 %7480
       %2345 = OpExtInst %v4float %1 Fma %2368 %297 %7481
       %2346 = OpConvertFToU %v4uint %2345
       %2348 = OpCompositeExtract %uint %2346 0
       %2350 = OpCompositeExtract %uint %2346 1
       %2351 = OpShiftLeftLogical %uint %2350 %int_10
       %2352 = OpBitwiseOr %uint %2348 %2351
       %2354 = OpCompositeExtract %uint %2346 2
       %2355 = OpShiftLeftLogical %uint %2354 %int_20
       %2356 = OpBitwiseOr %uint %2352 %2355
       %2358 = OpCompositeExtract %uint %2346 3
       %2359 = OpShiftLeftLogical %uint %2358 %int_30
       %2360 = OpBitwiseOr %uint %2356 %2359
               OpBranch %2286
       %2248 = OpLabel
       %2322 = OpExtInst %v4float %1 FClamp %2129 %7479 %7480
       %2297 = OpVectorTimesScalar %v4float %2322 %float_255
       %2299 = OpFAdd %v4float %2297 %7481
       %2300 = OpConvertFToU %v4uint %2299
       %2302 = OpCompositeExtract %uint %2300 0
       %2304 = OpCompositeExtract %uint %2300 1
       %2305 = OpShiftLeftLogical %uint %2304 %int_8
       %2306 = OpBitwiseOr %uint %2302 %2305
       %2308 = OpCompositeExtract %uint %2300 2
       %2309 = OpShiftLeftLogical %uint %2308 %int_16
       %2310 = OpBitwiseOr %uint %2306 %2309
       %2312 = OpCompositeExtract %uint %2300 3
       %2313 = OpShiftLeftLogical %uint %2312 %int_24
       %2314 = OpBitwiseOr %uint %2310 %2313
               OpBranch %2286
       %2244 = OpLabel
       %2246 = OpCompositeExtract %float %2129 0
       %2247 = OpBitcast %uint %2246
               OpBranch %2286
       %2286 = OpLabel
       %7193 = OpPhi %uint %2247 %2244 %2314 %2248 %2360 %2251 %2272 %2510 %2281 %2273 %2285 %2282
               OpSelectionMerge %2595 None
               OpBranchConditional %2056 %2587 %2589
       %2589 = OpLabel
       %2591 = OpIEqual %bool %1853 %uint_5
       %7494 = OpSelect %uint %2591 %uint_2 %uint_0
               OpBranch %2595
       %2587 = OpLabel
               OpBranch %2595
       %2595 = OpLabel
       %7211 = OpPhi %uint %1853 %2587 %7494 %2589
       %2604 = OpIAdd %uint %2072 %uint_1
       %2610 = OpCompositeConstruct %v2uint %2604 %2079
       %2613 = OpIAdd %v2uint %2610 %1791
       %2615 = OpShiftLeftLogical %v2uint %2613 %1264
       %2631 = OpCompositeConstruct %v2uint %7211 %7211
       %2624 = OpShiftRightLogical %v2uint %2631 %1065
       %2626 = OpBitwiseAnd %v2uint %2624 %7475
       %2618 = OpIAdd %v2uint %2615 %2626
       %2681 = OpCompositeExtract %uint %2618 0
       %2683 = OpUDiv %uint %2681 %2209
       %2685 = OpCompositeExtract %uint %2618 1
       %2687 = OpUDiv %uint %2685 %2214
       %2692 = OpIMul %uint %2683 %2209
       %2693 = OpISub %uint %2681 %2692
       %2698 = OpIMul %uint %2687 %2214
       %2699 = OpISub %uint %2685 %2698
       %2703 = OpIMul %uint %2687 %2172
       %2705 = OpIAdd %uint %2703 %2683
       %2709 = OpIAdd %uint %2177 %2705
       %2713 = OpISub %uint %2709 %2182
       %2718 = OpUDiv %uint %2713 %2185
       %2722 = OpIMul %uint %2718 %2185
       %2723 = OpISub %uint %2713 %2722
       %2726 = OpIMul %uint %2723 %2209
       %2728 = OpIAdd %uint %2726 %2693
       %2731 = OpIMul %uint %2718 %2214
       %2733 = OpIAdd %uint %2731 %2699
       %2749 = OpBitwiseAnd %uint %2733 %uint_1
       %2750 = OpINotEqual %bool %2749 %uint_0
               OpSelectionMerge %2757 None
               OpBranchConditional %2750 %2751 %2754
       %2754 = OpLabel
       %2755 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2756 = OpLoad %uint %2755
               OpBranch %2757
       %2751 = OpLabel
       %2752 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2753 = OpLoad %uint %2752
               OpBranch %2757
       %2757 = OpLabel
       %7213 = OpPhi %uint %2753 %2751 %2756 %2754
       %2648 = OpBitcast %int %2728
       %2651 = OpShiftRightLogical %uint %2733 %uint_1
       %2652 = OpBitcast %int %2651
       %2656 = OpCompositeConstruct %v2int %2648 %2652
       %2658 = OpBitcast %int %7213
       %2659 = OpImageFetch %v4float %2115 %2656 Sample %2658
               OpSelectionMerge %2816 None
               OpSwitch %1754 %2774 0 %2778 1 %2778 2 %2781 10 %2781 3 %2784 12 %2784 4 %2803 6 %2812
       %2812 = OpLabel
       %2814 = OpVectorShuffle %v2float %2659 %2659 0 1
       %2815 = OpExtInst %uint %1 PackHalf2x16 %2814
               OpBranch %2816
       %2803 = OpLabel
       %2805 = OpCompositeExtract %float %2659 0
       %3069 = OpExtInst %float %1 FMax %2805 %float_n1
       %3070 = OpExtInst %float %1 FMin %3069 %float_1
       %3072 = OpFOrdGreaterThanEqual %bool %3070 %float_0
       %3073 = OpSelect %float %3072 %float_0_5 %float_n0_5
       %3077 = OpExtInst %float %1 Fma %3070 %float_32767 %3073
       %3078 = OpConvertFToS %int %3077
       %3079 = OpBitcast %uint %3078
       %3080 = OpBitwiseAnd %uint %3079 %uint_65535
       %2808 = OpCompositeExtract %float %2659 1
       %3086 = OpExtInst %float %1 FMax %2808 %float_n1
       %3087 = OpExtInst %float %1 FMin %3086 %float_1
       %3089 = OpFOrdGreaterThanEqual %bool %3087 %float_0
       %3090 = OpSelect %float %3089 %float_0_5 %float_n0_5
       %3094 = OpExtInst %float %1 Fma %3087 %float_32767 %3090
       %3095 = OpConvertFToS %int %3094
       %3096 = OpBitcast %uint %3095
       %3097 = OpBitwiseAnd %uint %3096 %uint_65535
       %2810 = OpShiftLeftLogical %uint %3097 %uint_16
       %2811 = OpBitwiseOr %uint %3080 %2810
               OpBranch %2816
       %2784 = OpLabel
       %2786 = OpCompositeExtract %float %2659 0
       %2917 = OpExtInst %float %1 FMax %2786 %float_0
       %2918 = OpExtInst %float %1 FMin %2917 %float_31_875
       %2930 = OpBitcast %uint %2918
       %2932 = OpULessThan %bool %2930 %uint_1048576000
               OpSelectionMerge %2948 None
               OpBranchConditional %2932 %2933 %2945
       %2945 = OpLabel
       %2947 = OpIAdd %uint %2930 %uint_3254779904
               OpBranch %2948
       %2933 = OpLabel
       %2935 = OpShiftRightLogical %uint %2930 %uint_23
       %2937 = OpISub %uint %uint_125 %2935
       %2938 = OpExtInst %uint %1 UMin %2937 %uint_24
       %2940 = OpBitwiseAnd %uint %2930 %uint_8388607
       %2941 = OpBitwiseOr %uint %2940 %uint_8388608
       %2944 = OpShiftRightLogical %uint %2941 %2938
               OpBranch %2948
       %2948 = OpLabel
       %7214 = OpPhi %uint %2944 %2933 %2947 %2945
       %2950 = OpShiftRightLogical %uint %7214 %uint_16
       %2951 = OpBitwiseAnd %uint %2950 %uint_1
       %2953 = OpIAdd %uint %7214 %uint_32767
       %2955 = OpIAdd %uint %2953 %2951
       %2957 = OpShiftRightLogical %uint %2955 %uint_16
       %2958 = OpBitwiseAnd %uint %2957 %uint_1023
       %2789 = OpCompositeExtract %float %2659 1
       %2963 = OpExtInst %float %1 FMax %2789 %float_0
       %2964 = OpExtInst %float %1 FMin %2963 %float_31_875
       %2976 = OpBitcast %uint %2964
       %2978 = OpULessThan %bool %2976 %uint_1048576000
               OpSelectionMerge %2994 None
               OpBranchConditional %2978 %2979 %2991
       %2991 = OpLabel
       %2993 = OpIAdd %uint %2976 %uint_3254779904
               OpBranch %2994
       %2979 = OpLabel
       %2981 = OpShiftRightLogical %uint %2976 %uint_23
       %2983 = OpISub %uint %uint_125 %2981
       %2984 = OpExtInst %uint %1 UMin %2983 %uint_24
       %2986 = OpBitwiseAnd %uint %2976 %uint_8388607
       %2987 = OpBitwiseOr %uint %2986 %uint_8388608
       %2990 = OpShiftRightLogical %uint %2987 %2984
               OpBranch %2994
       %2994 = OpLabel
       %7215 = OpPhi %uint %2990 %2979 %2993 %2991
       %2996 = OpShiftRightLogical %uint %7215 %uint_16
       %2997 = OpBitwiseAnd %uint %2996 %uint_1
       %2999 = OpIAdd %uint %7215 %uint_32767
       %3001 = OpIAdd %uint %2999 %2997
       %3003 = OpShiftRightLogical %uint %3001 %uint_16
       %3004 = OpBitwiseAnd %uint %3003 %uint_1023
       %2791 = OpShiftLeftLogical %uint %3004 %uint_10
       %2792 = OpBitwiseOr %uint %2958 %2791
       %2794 = OpCompositeExtract %float %2659 2
       %3009 = OpExtInst %float %1 FMax %2794 %float_0
       %3010 = OpExtInst %float %1 FMin %3009 %float_31_875
       %3022 = OpBitcast %uint %3010
       %3024 = OpULessThan %bool %3022 %uint_1048576000
               OpSelectionMerge %3040 None
               OpBranchConditional %3024 %3025 %3037
       %3037 = OpLabel
       %3039 = OpIAdd %uint %3022 %uint_3254779904
               OpBranch %3040
       %3025 = OpLabel
       %3027 = OpShiftRightLogical %uint %3022 %uint_23
       %3029 = OpISub %uint %uint_125 %3027
       %3030 = OpExtInst %uint %1 UMin %3029 %uint_24
       %3032 = OpBitwiseAnd %uint %3022 %uint_8388607
       %3033 = OpBitwiseOr %uint %3032 %uint_8388608
       %3036 = OpShiftRightLogical %uint %3033 %3030
               OpBranch %3040
       %3040 = OpLabel
       %7216 = OpPhi %uint %3036 %3025 %3039 %3037
       %3042 = OpShiftRightLogical %uint %7216 %uint_16
       %3043 = OpBitwiseAnd %uint %3042 %uint_1
       %3045 = OpIAdd %uint %7216 %uint_32767
       %3047 = OpIAdd %uint %3045 %3043
       %3049 = OpShiftRightLogical %uint %3047 %uint_16
       %3050 = OpBitwiseAnd %uint %3049 %uint_1023
       %2796 = OpShiftLeftLogical %uint %3050 %uint_20
       %2797 = OpBitwiseOr %uint %2792 %2796
       %2799 = OpCompositeExtract %float %2659 3
       %3063 = OpExtInst %float %1 FClamp %2799 %float_0 %float_1
       %3058 = OpExtInst %float %1 Fma %3063 %float_3 %float_0_5
       %3059 = OpConvertFToU %uint %3058
       %2801 = OpShiftLeftLogical %uint %3059 %uint_30
       %2802 = OpBitwiseOr %uint %2797 %2801
               OpBranch %2816
       %2781 = OpLabel
       %2898 = OpExtInst %v4float %1 FClamp %2659 %7479 %7480
       %2875 = OpExtInst %v4float %1 Fma %2898 %297 %7481
       %2876 = OpConvertFToU %v4uint %2875
       %2878 = OpCompositeExtract %uint %2876 0
       %2880 = OpCompositeExtract %uint %2876 1
       %2881 = OpShiftLeftLogical %uint %2880 %int_10
       %2882 = OpBitwiseOr %uint %2878 %2881
       %2884 = OpCompositeExtract %uint %2876 2
       %2885 = OpShiftLeftLogical %uint %2884 %int_20
       %2886 = OpBitwiseOr %uint %2882 %2885
       %2888 = OpCompositeExtract %uint %2876 3
       %2889 = OpShiftLeftLogical %uint %2888 %int_30
       %2890 = OpBitwiseOr %uint %2886 %2889
               OpBranch %2816
       %2778 = OpLabel
       %2852 = OpExtInst %v4float %1 FClamp %2659 %7479 %7480
       %2827 = OpVectorTimesScalar %v4float %2852 %float_255
       %2829 = OpFAdd %v4float %2827 %7481
       %2830 = OpConvertFToU %v4uint %2829
       %2832 = OpCompositeExtract %uint %2830 0
       %2834 = OpCompositeExtract %uint %2830 1
       %2835 = OpShiftLeftLogical %uint %2834 %int_8
       %2836 = OpBitwiseOr %uint %2832 %2835
       %2838 = OpCompositeExtract %uint %2830 2
       %2839 = OpShiftLeftLogical %uint %2838 %int_16
       %2840 = OpBitwiseOr %uint %2836 %2839
       %2842 = OpCompositeExtract %uint %2830 3
       %2843 = OpShiftLeftLogical %uint %2842 %int_24
       %2844 = OpBitwiseOr %uint %2840 %2843
               OpBranch %2816
       %2774 = OpLabel
       %2776 = OpCompositeExtract %float %2659 0
       %2777 = OpBitcast %uint %2776
               OpBranch %2816
       %2816 = OpLabel
       %7219 = OpPhi %uint %2777 %2774 %2844 %2778 %2890 %2781 %2802 %3040 %2811 %2803 %2815 %2812
               OpSelectionMerge %3125 None
               OpBranchConditional %2056 %3117 %3119
       %3119 = OpLabel
       %3121 = OpIEqual %bool %1853 %uint_5
       %7495 = OpSelect %uint %3121 %uint_2 %uint_0
               OpBranch %3125
       %3117 = OpLabel
               OpBranch %3125
       %3125 = OpLabel
       %7231 = OpPhi %uint %1853 %3117 %7495 %3119
       %3134 = OpIAdd %uint %2072 %uint_2
       %3140 = OpCompositeConstruct %v2uint %3134 %2079
       %3143 = OpIAdd %v2uint %3140 %1791
       %3145 = OpShiftLeftLogical %v2uint %3143 %1264
       %3161 = OpCompositeConstruct %v2uint %7231 %7231
       %3154 = OpShiftRightLogical %v2uint %3161 %1065
       %3156 = OpBitwiseAnd %v2uint %3154 %7475
       %3148 = OpIAdd %v2uint %3145 %3156
       %3211 = OpCompositeExtract %uint %3148 0
       %3213 = OpUDiv %uint %3211 %2209
       %3215 = OpCompositeExtract %uint %3148 1
       %3217 = OpUDiv %uint %3215 %2214
       %3222 = OpIMul %uint %3213 %2209
       %3223 = OpISub %uint %3211 %3222
       %3228 = OpIMul %uint %3217 %2214
       %3229 = OpISub %uint %3215 %3228
       %3233 = OpIMul %uint %3217 %2172
       %3235 = OpIAdd %uint %3233 %3213
       %3239 = OpIAdd %uint %2177 %3235
       %3243 = OpISub %uint %3239 %2182
       %3248 = OpUDiv %uint %3243 %2185
       %3252 = OpIMul %uint %3248 %2185
       %3253 = OpISub %uint %3243 %3252
       %3256 = OpIMul %uint %3253 %2209
       %3258 = OpIAdd %uint %3256 %3223
       %3261 = OpIMul %uint %3248 %2214
       %3263 = OpIAdd %uint %3261 %3229
       %3279 = OpBitwiseAnd %uint %3263 %uint_1
       %3280 = OpINotEqual %bool %3279 %uint_0
               OpSelectionMerge %3287 None
               OpBranchConditional %3280 %3281 %3284
       %3284 = OpLabel
       %3285 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %3286 = OpLoad %uint %3285
               OpBranch %3287
       %3281 = OpLabel
       %3282 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %3283 = OpLoad %uint %3282
               OpBranch %3287
       %3287 = OpLabel
       %7233 = OpPhi %uint %3283 %3281 %3286 %3284
       %3178 = OpBitcast %int %3258
       %3181 = OpShiftRightLogical %uint %3263 %uint_1
       %3182 = OpBitcast %int %3181
       %3186 = OpCompositeConstruct %v2int %3178 %3182
       %3188 = OpBitcast %int %7233
       %3189 = OpImageFetch %v4float %2115 %3186 Sample %3188
               OpSelectionMerge %3346 None
               OpSwitch %1754 %3304 0 %3308 1 %3308 2 %3311 10 %3311 3 %3314 12 %3314 4 %3333 6 %3342
       %3342 = OpLabel
       %3344 = OpVectorShuffle %v2float %3189 %3189 0 1
       %3345 = OpExtInst %uint %1 PackHalf2x16 %3344
               OpBranch %3346
       %3333 = OpLabel
       %3335 = OpCompositeExtract %float %3189 0
       %3599 = OpExtInst %float %1 FMax %3335 %float_n1
       %3600 = OpExtInst %float %1 FMin %3599 %float_1
       %3602 = OpFOrdGreaterThanEqual %bool %3600 %float_0
       %3603 = OpSelect %float %3602 %float_0_5 %float_n0_5
       %3607 = OpExtInst %float %1 Fma %3600 %float_32767 %3603
       %3608 = OpConvertFToS %int %3607
       %3609 = OpBitcast %uint %3608
       %3610 = OpBitwiseAnd %uint %3609 %uint_65535
       %3338 = OpCompositeExtract %float %3189 1
       %3616 = OpExtInst %float %1 FMax %3338 %float_n1
       %3617 = OpExtInst %float %1 FMin %3616 %float_1
       %3619 = OpFOrdGreaterThanEqual %bool %3617 %float_0
       %3620 = OpSelect %float %3619 %float_0_5 %float_n0_5
       %3624 = OpExtInst %float %1 Fma %3617 %float_32767 %3620
       %3625 = OpConvertFToS %int %3624
       %3626 = OpBitcast %uint %3625
       %3627 = OpBitwiseAnd %uint %3626 %uint_65535
       %3340 = OpShiftLeftLogical %uint %3627 %uint_16
       %3341 = OpBitwiseOr %uint %3610 %3340
               OpBranch %3346
       %3314 = OpLabel
       %3316 = OpCompositeExtract %float %3189 0
       %3447 = OpExtInst %float %1 FMax %3316 %float_0
       %3448 = OpExtInst %float %1 FMin %3447 %float_31_875
       %3460 = OpBitcast %uint %3448
       %3462 = OpULessThan %bool %3460 %uint_1048576000
               OpSelectionMerge %3478 None
               OpBranchConditional %3462 %3463 %3475
       %3475 = OpLabel
       %3477 = OpIAdd %uint %3460 %uint_3254779904
               OpBranch %3478
       %3463 = OpLabel
       %3465 = OpShiftRightLogical %uint %3460 %uint_23
       %3467 = OpISub %uint %uint_125 %3465
       %3468 = OpExtInst %uint %1 UMin %3467 %uint_24
       %3470 = OpBitwiseAnd %uint %3460 %uint_8388607
       %3471 = OpBitwiseOr %uint %3470 %uint_8388608
       %3474 = OpShiftRightLogical %uint %3471 %3468
               OpBranch %3478
       %3478 = OpLabel
       %7234 = OpPhi %uint %3474 %3463 %3477 %3475
       %3480 = OpShiftRightLogical %uint %7234 %uint_16
       %3481 = OpBitwiseAnd %uint %3480 %uint_1
       %3483 = OpIAdd %uint %7234 %uint_32767
       %3485 = OpIAdd %uint %3483 %3481
       %3487 = OpShiftRightLogical %uint %3485 %uint_16
       %3488 = OpBitwiseAnd %uint %3487 %uint_1023
       %3319 = OpCompositeExtract %float %3189 1
       %3493 = OpExtInst %float %1 FMax %3319 %float_0
       %3494 = OpExtInst %float %1 FMin %3493 %float_31_875
       %3506 = OpBitcast %uint %3494
       %3508 = OpULessThan %bool %3506 %uint_1048576000
               OpSelectionMerge %3524 None
               OpBranchConditional %3508 %3509 %3521
       %3521 = OpLabel
       %3523 = OpIAdd %uint %3506 %uint_3254779904
               OpBranch %3524
       %3509 = OpLabel
       %3511 = OpShiftRightLogical %uint %3506 %uint_23
       %3513 = OpISub %uint %uint_125 %3511
       %3514 = OpExtInst %uint %1 UMin %3513 %uint_24
       %3516 = OpBitwiseAnd %uint %3506 %uint_8388607
       %3517 = OpBitwiseOr %uint %3516 %uint_8388608
       %3520 = OpShiftRightLogical %uint %3517 %3514
               OpBranch %3524
       %3524 = OpLabel
       %7235 = OpPhi %uint %3520 %3509 %3523 %3521
       %3526 = OpShiftRightLogical %uint %7235 %uint_16
       %3527 = OpBitwiseAnd %uint %3526 %uint_1
       %3529 = OpIAdd %uint %7235 %uint_32767
       %3531 = OpIAdd %uint %3529 %3527
       %3533 = OpShiftRightLogical %uint %3531 %uint_16
       %3534 = OpBitwiseAnd %uint %3533 %uint_1023
       %3321 = OpShiftLeftLogical %uint %3534 %uint_10
       %3322 = OpBitwiseOr %uint %3488 %3321
       %3324 = OpCompositeExtract %float %3189 2
       %3539 = OpExtInst %float %1 FMax %3324 %float_0
       %3540 = OpExtInst %float %1 FMin %3539 %float_31_875
       %3552 = OpBitcast %uint %3540
       %3554 = OpULessThan %bool %3552 %uint_1048576000
               OpSelectionMerge %3570 None
               OpBranchConditional %3554 %3555 %3567
       %3567 = OpLabel
       %3569 = OpIAdd %uint %3552 %uint_3254779904
               OpBranch %3570
       %3555 = OpLabel
       %3557 = OpShiftRightLogical %uint %3552 %uint_23
       %3559 = OpISub %uint %uint_125 %3557
       %3560 = OpExtInst %uint %1 UMin %3559 %uint_24
       %3562 = OpBitwiseAnd %uint %3552 %uint_8388607
       %3563 = OpBitwiseOr %uint %3562 %uint_8388608
       %3566 = OpShiftRightLogical %uint %3563 %3560
               OpBranch %3570
       %3570 = OpLabel
       %7236 = OpPhi %uint %3566 %3555 %3569 %3567
       %3572 = OpShiftRightLogical %uint %7236 %uint_16
       %3573 = OpBitwiseAnd %uint %3572 %uint_1
       %3575 = OpIAdd %uint %7236 %uint_32767
       %3577 = OpIAdd %uint %3575 %3573
       %3579 = OpShiftRightLogical %uint %3577 %uint_16
       %3580 = OpBitwiseAnd %uint %3579 %uint_1023
       %3326 = OpShiftLeftLogical %uint %3580 %uint_20
       %3327 = OpBitwiseOr %uint %3322 %3326
       %3329 = OpCompositeExtract %float %3189 3
       %3593 = OpExtInst %float %1 FClamp %3329 %float_0 %float_1
       %3588 = OpExtInst %float %1 Fma %3593 %float_3 %float_0_5
       %3589 = OpConvertFToU %uint %3588
       %3331 = OpShiftLeftLogical %uint %3589 %uint_30
       %3332 = OpBitwiseOr %uint %3327 %3331
               OpBranch %3346
       %3311 = OpLabel
       %3428 = OpExtInst %v4float %1 FClamp %3189 %7479 %7480
       %3405 = OpExtInst %v4float %1 Fma %3428 %297 %7481
       %3406 = OpConvertFToU %v4uint %3405
       %3408 = OpCompositeExtract %uint %3406 0
       %3410 = OpCompositeExtract %uint %3406 1
       %3411 = OpShiftLeftLogical %uint %3410 %int_10
       %3412 = OpBitwiseOr %uint %3408 %3411
       %3414 = OpCompositeExtract %uint %3406 2
       %3415 = OpShiftLeftLogical %uint %3414 %int_20
       %3416 = OpBitwiseOr %uint %3412 %3415
       %3418 = OpCompositeExtract %uint %3406 3
       %3419 = OpShiftLeftLogical %uint %3418 %int_30
       %3420 = OpBitwiseOr %uint %3416 %3419
               OpBranch %3346
       %3308 = OpLabel
       %3382 = OpExtInst %v4float %1 FClamp %3189 %7479 %7480
       %3357 = OpVectorTimesScalar %v4float %3382 %float_255
       %3359 = OpFAdd %v4float %3357 %7481
       %3360 = OpConvertFToU %v4uint %3359
       %3362 = OpCompositeExtract %uint %3360 0
       %3364 = OpCompositeExtract %uint %3360 1
       %3365 = OpShiftLeftLogical %uint %3364 %int_8
       %3366 = OpBitwiseOr %uint %3362 %3365
       %3368 = OpCompositeExtract %uint %3360 2
       %3369 = OpShiftLeftLogical %uint %3368 %int_16
       %3370 = OpBitwiseOr %uint %3366 %3369
       %3372 = OpCompositeExtract %uint %3360 3
       %3373 = OpShiftLeftLogical %uint %3372 %int_24
       %3374 = OpBitwiseOr %uint %3370 %3373
               OpBranch %3346
       %3304 = OpLabel
       %3306 = OpCompositeExtract %float %3189 0
       %3307 = OpBitcast %uint %3306
               OpBranch %3346
       %3346 = OpLabel
       %7239 = OpPhi %uint %3307 %3304 %3374 %3308 %3420 %3311 %3332 %3570 %3341 %3333 %3345 %3342
               OpSelectionMerge %3655 None
               OpBranchConditional %2056 %3647 %3649
       %3649 = OpLabel
       %3651 = OpIEqual %bool %1853 %uint_5
       %7496 = OpSelect %uint %3651 %uint_2 %uint_0
               OpBranch %3655
       %3647 = OpLabel
               OpBranch %3655
       %3655 = OpLabel
       %7251 = OpPhi %uint %1853 %3647 %7496 %3649
       %3664 = OpIAdd %uint %2072 %uint_3
       %3670 = OpCompositeConstruct %v2uint %3664 %2079
       %3673 = OpIAdd %v2uint %3670 %1791
       %3675 = OpShiftLeftLogical %v2uint %3673 %1264
       %3691 = OpCompositeConstruct %v2uint %7251 %7251
       %3684 = OpShiftRightLogical %v2uint %3691 %1065
       %3686 = OpBitwiseAnd %v2uint %3684 %7475
       %3678 = OpIAdd %v2uint %3675 %3686
       %3741 = OpCompositeExtract %uint %3678 0
       %3743 = OpUDiv %uint %3741 %2209
       %3745 = OpCompositeExtract %uint %3678 1
       %3747 = OpUDiv %uint %3745 %2214
       %3752 = OpIMul %uint %3743 %2209
       %3753 = OpISub %uint %3741 %3752
       %3758 = OpIMul %uint %3747 %2214
       %3759 = OpISub %uint %3745 %3758
       %3763 = OpIMul %uint %3747 %2172
       %3765 = OpIAdd %uint %3763 %3743
       %3769 = OpIAdd %uint %2177 %3765
       %3773 = OpISub %uint %3769 %2182
       %3778 = OpUDiv %uint %3773 %2185
       %3782 = OpIMul %uint %3778 %2185
       %3783 = OpISub %uint %3773 %3782
       %3786 = OpIMul %uint %3783 %2209
       %3788 = OpIAdd %uint %3786 %3753
       %3791 = OpIMul %uint %3778 %2214
       %3793 = OpIAdd %uint %3791 %3759
       %3809 = OpBitwiseAnd %uint %3793 %uint_1
       %3810 = OpINotEqual %bool %3809 %uint_0
               OpSelectionMerge %3817 None
               OpBranchConditional %3810 %3811 %3814
       %3814 = OpLabel
       %3815 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %3816 = OpLoad %uint %3815
               OpBranch %3817
       %3811 = OpLabel
       %3812 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %3813 = OpLoad %uint %3812
               OpBranch %3817
       %3817 = OpLabel
       %7253 = OpPhi %uint %3813 %3811 %3816 %3814
       %3708 = OpBitcast %int %3788
       %3711 = OpShiftRightLogical %uint %3793 %uint_1
       %3712 = OpBitcast %int %3711
       %3716 = OpCompositeConstruct %v2int %3708 %3712
       %3718 = OpBitcast %int %7253
       %3719 = OpImageFetch %v4float %2115 %3716 Sample %3718
               OpSelectionMerge %3876 None
               OpSwitch %1754 %3834 0 %3838 1 %3838 2 %3841 10 %3841 3 %3844 12 %3844 4 %3863 6 %3872
       %3872 = OpLabel
       %3874 = OpVectorShuffle %v2float %3719 %3719 0 1
       %3875 = OpExtInst %uint %1 PackHalf2x16 %3874
               OpBranch %3876
       %3863 = OpLabel
       %3865 = OpCompositeExtract %float %3719 0
       %4129 = OpExtInst %float %1 FMax %3865 %float_n1
       %4130 = OpExtInst %float %1 FMin %4129 %float_1
       %4132 = OpFOrdGreaterThanEqual %bool %4130 %float_0
       %4133 = OpSelect %float %4132 %float_0_5 %float_n0_5
       %4137 = OpExtInst %float %1 Fma %4130 %float_32767 %4133
       %4138 = OpConvertFToS %int %4137
       %4139 = OpBitcast %uint %4138
       %4140 = OpBitwiseAnd %uint %4139 %uint_65535
       %3868 = OpCompositeExtract %float %3719 1
       %4146 = OpExtInst %float %1 FMax %3868 %float_n1
       %4147 = OpExtInst %float %1 FMin %4146 %float_1
       %4149 = OpFOrdGreaterThanEqual %bool %4147 %float_0
       %4150 = OpSelect %float %4149 %float_0_5 %float_n0_5
       %4154 = OpExtInst %float %1 Fma %4147 %float_32767 %4150
       %4155 = OpConvertFToS %int %4154
       %4156 = OpBitcast %uint %4155
       %4157 = OpBitwiseAnd %uint %4156 %uint_65535
       %3870 = OpShiftLeftLogical %uint %4157 %uint_16
       %3871 = OpBitwiseOr %uint %4140 %3870
               OpBranch %3876
       %3844 = OpLabel
       %3846 = OpCompositeExtract %float %3719 0
       %3977 = OpExtInst %float %1 FMax %3846 %float_0
       %3978 = OpExtInst %float %1 FMin %3977 %float_31_875
       %3990 = OpBitcast %uint %3978
       %3992 = OpULessThan %bool %3990 %uint_1048576000
               OpSelectionMerge %4008 None
               OpBranchConditional %3992 %3993 %4005
       %4005 = OpLabel
       %4007 = OpIAdd %uint %3990 %uint_3254779904
               OpBranch %4008
       %3993 = OpLabel
       %3995 = OpShiftRightLogical %uint %3990 %uint_23
       %3997 = OpISub %uint %uint_125 %3995
       %3998 = OpExtInst %uint %1 UMin %3997 %uint_24
       %4000 = OpBitwiseAnd %uint %3990 %uint_8388607
       %4001 = OpBitwiseOr %uint %4000 %uint_8388608
       %4004 = OpShiftRightLogical %uint %4001 %3998
               OpBranch %4008
       %4008 = OpLabel
       %7254 = OpPhi %uint %4004 %3993 %4007 %4005
       %4010 = OpShiftRightLogical %uint %7254 %uint_16
       %4011 = OpBitwiseAnd %uint %4010 %uint_1
       %4013 = OpIAdd %uint %7254 %uint_32767
       %4015 = OpIAdd %uint %4013 %4011
       %4017 = OpShiftRightLogical %uint %4015 %uint_16
       %4018 = OpBitwiseAnd %uint %4017 %uint_1023
       %3849 = OpCompositeExtract %float %3719 1
       %4023 = OpExtInst %float %1 FMax %3849 %float_0
       %4024 = OpExtInst %float %1 FMin %4023 %float_31_875
       %4036 = OpBitcast %uint %4024
       %4038 = OpULessThan %bool %4036 %uint_1048576000
               OpSelectionMerge %4054 None
               OpBranchConditional %4038 %4039 %4051
       %4051 = OpLabel
       %4053 = OpIAdd %uint %4036 %uint_3254779904
               OpBranch %4054
       %4039 = OpLabel
       %4041 = OpShiftRightLogical %uint %4036 %uint_23
       %4043 = OpISub %uint %uint_125 %4041
       %4044 = OpExtInst %uint %1 UMin %4043 %uint_24
       %4046 = OpBitwiseAnd %uint %4036 %uint_8388607
       %4047 = OpBitwiseOr %uint %4046 %uint_8388608
       %4050 = OpShiftRightLogical %uint %4047 %4044
               OpBranch %4054
       %4054 = OpLabel
       %7255 = OpPhi %uint %4050 %4039 %4053 %4051
       %4056 = OpShiftRightLogical %uint %7255 %uint_16
       %4057 = OpBitwiseAnd %uint %4056 %uint_1
       %4059 = OpIAdd %uint %7255 %uint_32767
       %4061 = OpIAdd %uint %4059 %4057
       %4063 = OpShiftRightLogical %uint %4061 %uint_16
       %4064 = OpBitwiseAnd %uint %4063 %uint_1023
       %3851 = OpShiftLeftLogical %uint %4064 %uint_10
       %3852 = OpBitwiseOr %uint %4018 %3851
       %3854 = OpCompositeExtract %float %3719 2
       %4069 = OpExtInst %float %1 FMax %3854 %float_0
       %4070 = OpExtInst %float %1 FMin %4069 %float_31_875
       %4082 = OpBitcast %uint %4070
       %4084 = OpULessThan %bool %4082 %uint_1048576000
               OpSelectionMerge %4100 None
               OpBranchConditional %4084 %4085 %4097
       %4097 = OpLabel
       %4099 = OpIAdd %uint %4082 %uint_3254779904
               OpBranch %4100
       %4085 = OpLabel
       %4087 = OpShiftRightLogical %uint %4082 %uint_23
       %4089 = OpISub %uint %uint_125 %4087
       %4090 = OpExtInst %uint %1 UMin %4089 %uint_24
       %4092 = OpBitwiseAnd %uint %4082 %uint_8388607
       %4093 = OpBitwiseOr %uint %4092 %uint_8388608
       %4096 = OpShiftRightLogical %uint %4093 %4090
               OpBranch %4100
       %4100 = OpLabel
       %7256 = OpPhi %uint %4096 %4085 %4099 %4097
       %4102 = OpShiftRightLogical %uint %7256 %uint_16
       %4103 = OpBitwiseAnd %uint %4102 %uint_1
       %4105 = OpIAdd %uint %7256 %uint_32767
       %4107 = OpIAdd %uint %4105 %4103
       %4109 = OpShiftRightLogical %uint %4107 %uint_16
       %4110 = OpBitwiseAnd %uint %4109 %uint_1023
       %3856 = OpShiftLeftLogical %uint %4110 %uint_20
       %3857 = OpBitwiseOr %uint %3852 %3856
       %3859 = OpCompositeExtract %float %3719 3
       %4123 = OpExtInst %float %1 FClamp %3859 %float_0 %float_1
       %4118 = OpExtInst %float %1 Fma %4123 %float_3 %float_0_5
       %4119 = OpConvertFToU %uint %4118
       %3861 = OpShiftLeftLogical %uint %4119 %uint_30
       %3862 = OpBitwiseOr %uint %3857 %3861
               OpBranch %3876
       %3841 = OpLabel
       %3958 = OpExtInst %v4float %1 FClamp %3719 %7479 %7480
       %3935 = OpExtInst %v4float %1 Fma %3958 %297 %7481
       %3936 = OpConvertFToU %v4uint %3935
       %3938 = OpCompositeExtract %uint %3936 0
       %3940 = OpCompositeExtract %uint %3936 1
       %3941 = OpShiftLeftLogical %uint %3940 %int_10
       %3942 = OpBitwiseOr %uint %3938 %3941
       %3944 = OpCompositeExtract %uint %3936 2
       %3945 = OpShiftLeftLogical %uint %3944 %int_20
       %3946 = OpBitwiseOr %uint %3942 %3945
       %3948 = OpCompositeExtract %uint %3936 3
       %3949 = OpShiftLeftLogical %uint %3948 %int_30
       %3950 = OpBitwiseOr %uint %3946 %3949
               OpBranch %3876
       %3838 = OpLabel
       %3912 = OpExtInst %v4float %1 FClamp %3719 %7479 %7480
       %3887 = OpVectorTimesScalar %v4float %3912 %float_255
       %3889 = OpFAdd %v4float %3887 %7481
       %3890 = OpConvertFToU %v4uint %3889
       %3892 = OpCompositeExtract %uint %3890 0
       %3894 = OpCompositeExtract %uint %3890 1
       %3895 = OpShiftLeftLogical %uint %3894 %int_8
       %3896 = OpBitwiseOr %uint %3892 %3895
       %3898 = OpCompositeExtract %uint %3890 2
       %3899 = OpShiftLeftLogical %uint %3898 %int_16
       %3900 = OpBitwiseOr %uint %3896 %3899
       %3902 = OpCompositeExtract %uint %3890 3
       %3903 = OpShiftLeftLogical %uint %3902 %int_24
       %3904 = OpBitwiseOr %uint %3900 %3903
               OpBranch %3876
       %3834 = OpLabel
       %3836 = OpCompositeExtract %float %3719 0
       %3837 = OpBitcast %uint %3836
               OpBranch %3876
       %3876 = OpLabel
       %7259 = OpPhi %uint %3837 %3834 %3904 %3838 %3950 %3841 %3862 %4100 %3871 %3863 %3875 %3872
       %7491 = OpCompositeConstruct %v4uint %7193 %7219 %7239 %7259
               OpSelectionMerge %4185 None
               OpBranchConditional %2056 %4177 %4179
       %4179 = OpLabel
       %4181 = OpIEqual %bool %1853 %uint_5
       %7497 = OpSelect %uint %4181 %uint_2 %uint_0
               OpBranch %4185
       %4177 = OpLabel
               OpBranch %4185
       %4185 = OpLabel
       %7271 = OpPhi %uint %1853 %4177 %7497 %4179
       %4194 = OpIAdd %uint %2072 %uint_4
       %4200 = OpCompositeConstruct %v2uint %4194 %2079
       %4203 = OpIAdd %v2uint %4200 %1791
       %4205 = OpShiftLeftLogical %v2uint %4203 %1264
       %4221 = OpCompositeConstruct %v2uint %7271 %7271
       %4214 = OpShiftRightLogical %v2uint %4221 %1065
       %4216 = OpBitwiseAnd %v2uint %4214 %7475
       %4208 = OpIAdd %v2uint %4205 %4216
       %4271 = OpCompositeExtract %uint %4208 0
       %4273 = OpUDiv %uint %4271 %2209
       %4275 = OpCompositeExtract %uint %4208 1
       %4277 = OpUDiv %uint %4275 %2214
       %4282 = OpIMul %uint %4273 %2209
       %4283 = OpISub %uint %4271 %4282
       %4288 = OpIMul %uint %4277 %2214
       %4289 = OpISub %uint %4275 %4288
       %4293 = OpIMul %uint %4277 %2172
       %4295 = OpIAdd %uint %4293 %4273
       %4299 = OpIAdd %uint %2177 %4295
       %4303 = OpISub %uint %4299 %2182
       %4308 = OpUDiv %uint %4303 %2185
       %4312 = OpIMul %uint %4308 %2185
       %4313 = OpISub %uint %4303 %4312
       %4316 = OpIMul %uint %4313 %2209
       %4318 = OpIAdd %uint %4316 %4283
       %4321 = OpIMul %uint %4308 %2214
       %4323 = OpIAdd %uint %4321 %4289
       %4339 = OpBitwiseAnd %uint %4323 %uint_1
       %4340 = OpINotEqual %bool %4339 %uint_0
               OpSelectionMerge %4347 None
               OpBranchConditional %4340 %4341 %4344
       %4344 = OpLabel
       %4345 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %4346 = OpLoad %uint %4345
               OpBranch %4347
       %4341 = OpLabel
       %4342 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %4343 = OpLoad %uint %4342
               OpBranch %4347
       %4347 = OpLabel
       %7273 = OpPhi %uint %4343 %4341 %4346 %4344
       %4238 = OpBitcast %int %4318
       %4241 = OpShiftRightLogical %uint %4323 %uint_1
       %4242 = OpBitcast %int %4241
       %4246 = OpCompositeConstruct %v2int %4238 %4242
       %4248 = OpBitcast %int %7273
       %4249 = OpImageFetch %v4float %2115 %4246 Sample %4248
               OpSelectionMerge %4406 None
               OpSwitch %1754 %4364 0 %4368 1 %4368 2 %4371 10 %4371 3 %4374 12 %4374 4 %4393 6 %4402
       %4402 = OpLabel
       %4404 = OpVectorShuffle %v2float %4249 %4249 0 1
       %4405 = OpExtInst %uint %1 PackHalf2x16 %4404
               OpBranch %4406
       %4393 = OpLabel
       %4395 = OpCompositeExtract %float %4249 0
       %4659 = OpExtInst %float %1 FMax %4395 %float_n1
       %4660 = OpExtInst %float %1 FMin %4659 %float_1
       %4662 = OpFOrdGreaterThanEqual %bool %4660 %float_0
       %4663 = OpSelect %float %4662 %float_0_5 %float_n0_5
       %4667 = OpExtInst %float %1 Fma %4660 %float_32767 %4663
       %4668 = OpConvertFToS %int %4667
       %4669 = OpBitcast %uint %4668
       %4670 = OpBitwiseAnd %uint %4669 %uint_65535
       %4398 = OpCompositeExtract %float %4249 1
       %4676 = OpExtInst %float %1 FMax %4398 %float_n1
       %4677 = OpExtInst %float %1 FMin %4676 %float_1
       %4679 = OpFOrdGreaterThanEqual %bool %4677 %float_0
       %4680 = OpSelect %float %4679 %float_0_5 %float_n0_5
       %4684 = OpExtInst %float %1 Fma %4677 %float_32767 %4680
       %4685 = OpConvertFToS %int %4684
       %4686 = OpBitcast %uint %4685
       %4687 = OpBitwiseAnd %uint %4686 %uint_65535
       %4400 = OpShiftLeftLogical %uint %4687 %uint_16
       %4401 = OpBitwiseOr %uint %4670 %4400
               OpBranch %4406
       %4374 = OpLabel
       %4376 = OpCompositeExtract %float %4249 0
       %4507 = OpExtInst %float %1 FMax %4376 %float_0
       %4508 = OpExtInst %float %1 FMin %4507 %float_31_875
       %4520 = OpBitcast %uint %4508
       %4522 = OpULessThan %bool %4520 %uint_1048576000
               OpSelectionMerge %4538 None
               OpBranchConditional %4522 %4523 %4535
       %4535 = OpLabel
       %4537 = OpIAdd %uint %4520 %uint_3254779904
               OpBranch %4538
       %4523 = OpLabel
       %4525 = OpShiftRightLogical %uint %4520 %uint_23
       %4527 = OpISub %uint %uint_125 %4525
       %4528 = OpExtInst %uint %1 UMin %4527 %uint_24
       %4530 = OpBitwiseAnd %uint %4520 %uint_8388607
       %4531 = OpBitwiseOr %uint %4530 %uint_8388608
       %4534 = OpShiftRightLogical %uint %4531 %4528
               OpBranch %4538
       %4538 = OpLabel
       %7274 = OpPhi %uint %4534 %4523 %4537 %4535
       %4540 = OpShiftRightLogical %uint %7274 %uint_16
       %4541 = OpBitwiseAnd %uint %4540 %uint_1
       %4543 = OpIAdd %uint %7274 %uint_32767
       %4545 = OpIAdd %uint %4543 %4541
       %4547 = OpShiftRightLogical %uint %4545 %uint_16
       %4548 = OpBitwiseAnd %uint %4547 %uint_1023
       %4379 = OpCompositeExtract %float %4249 1
       %4553 = OpExtInst %float %1 FMax %4379 %float_0
       %4554 = OpExtInst %float %1 FMin %4553 %float_31_875
       %4566 = OpBitcast %uint %4554
       %4568 = OpULessThan %bool %4566 %uint_1048576000
               OpSelectionMerge %4584 None
               OpBranchConditional %4568 %4569 %4581
       %4581 = OpLabel
       %4583 = OpIAdd %uint %4566 %uint_3254779904
               OpBranch %4584
       %4569 = OpLabel
       %4571 = OpShiftRightLogical %uint %4566 %uint_23
       %4573 = OpISub %uint %uint_125 %4571
       %4574 = OpExtInst %uint %1 UMin %4573 %uint_24
       %4576 = OpBitwiseAnd %uint %4566 %uint_8388607
       %4577 = OpBitwiseOr %uint %4576 %uint_8388608
       %4580 = OpShiftRightLogical %uint %4577 %4574
               OpBranch %4584
       %4584 = OpLabel
       %7275 = OpPhi %uint %4580 %4569 %4583 %4581
       %4586 = OpShiftRightLogical %uint %7275 %uint_16
       %4587 = OpBitwiseAnd %uint %4586 %uint_1
       %4589 = OpIAdd %uint %7275 %uint_32767
       %4591 = OpIAdd %uint %4589 %4587
       %4593 = OpShiftRightLogical %uint %4591 %uint_16
       %4594 = OpBitwiseAnd %uint %4593 %uint_1023
       %4381 = OpShiftLeftLogical %uint %4594 %uint_10
       %4382 = OpBitwiseOr %uint %4548 %4381
       %4384 = OpCompositeExtract %float %4249 2
       %4599 = OpExtInst %float %1 FMax %4384 %float_0
       %4600 = OpExtInst %float %1 FMin %4599 %float_31_875
       %4612 = OpBitcast %uint %4600
       %4614 = OpULessThan %bool %4612 %uint_1048576000
               OpSelectionMerge %4630 None
               OpBranchConditional %4614 %4615 %4627
       %4627 = OpLabel
       %4629 = OpIAdd %uint %4612 %uint_3254779904
               OpBranch %4630
       %4615 = OpLabel
       %4617 = OpShiftRightLogical %uint %4612 %uint_23
       %4619 = OpISub %uint %uint_125 %4617
       %4620 = OpExtInst %uint %1 UMin %4619 %uint_24
       %4622 = OpBitwiseAnd %uint %4612 %uint_8388607
       %4623 = OpBitwiseOr %uint %4622 %uint_8388608
       %4626 = OpShiftRightLogical %uint %4623 %4620
               OpBranch %4630
       %4630 = OpLabel
       %7276 = OpPhi %uint %4626 %4615 %4629 %4627
       %4632 = OpShiftRightLogical %uint %7276 %uint_16
       %4633 = OpBitwiseAnd %uint %4632 %uint_1
       %4635 = OpIAdd %uint %7276 %uint_32767
       %4637 = OpIAdd %uint %4635 %4633
       %4639 = OpShiftRightLogical %uint %4637 %uint_16
       %4640 = OpBitwiseAnd %uint %4639 %uint_1023
       %4386 = OpShiftLeftLogical %uint %4640 %uint_20
       %4387 = OpBitwiseOr %uint %4382 %4386
       %4389 = OpCompositeExtract %float %4249 3
       %4653 = OpExtInst %float %1 FClamp %4389 %float_0 %float_1
       %4648 = OpExtInst %float %1 Fma %4653 %float_3 %float_0_5
       %4649 = OpConvertFToU %uint %4648
       %4391 = OpShiftLeftLogical %uint %4649 %uint_30
       %4392 = OpBitwiseOr %uint %4387 %4391
               OpBranch %4406
       %4371 = OpLabel
       %4488 = OpExtInst %v4float %1 FClamp %4249 %7479 %7480
       %4465 = OpExtInst %v4float %1 Fma %4488 %297 %7481
       %4466 = OpConvertFToU %v4uint %4465
       %4468 = OpCompositeExtract %uint %4466 0
       %4470 = OpCompositeExtract %uint %4466 1
       %4471 = OpShiftLeftLogical %uint %4470 %int_10
       %4472 = OpBitwiseOr %uint %4468 %4471
       %4474 = OpCompositeExtract %uint %4466 2
       %4475 = OpShiftLeftLogical %uint %4474 %int_20
       %4476 = OpBitwiseOr %uint %4472 %4475
       %4478 = OpCompositeExtract %uint %4466 3
       %4479 = OpShiftLeftLogical %uint %4478 %int_30
       %4480 = OpBitwiseOr %uint %4476 %4479
               OpBranch %4406
       %4368 = OpLabel
       %4442 = OpExtInst %v4float %1 FClamp %4249 %7479 %7480
       %4417 = OpVectorTimesScalar %v4float %4442 %float_255
       %4419 = OpFAdd %v4float %4417 %7481
       %4420 = OpConvertFToU %v4uint %4419
       %4422 = OpCompositeExtract %uint %4420 0
       %4424 = OpCompositeExtract %uint %4420 1
       %4425 = OpShiftLeftLogical %uint %4424 %int_8
       %4426 = OpBitwiseOr %uint %4422 %4425
       %4428 = OpCompositeExtract %uint %4420 2
       %4429 = OpShiftLeftLogical %uint %4428 %int_16
       %4430 = OpBitwiseOr %uint %4426 %4429
       %4432 = OpCompositeExtract %uint %4420 3
       %4433 = OpShiftLeftLogical %uint %4432 %int_24
       %4434 = OpBitwiseOr %uint %4430 %4433
               OpBranch %4406
       %4364 = OpLabel
       %4366 = OpCompositeExtract %float %4249 0
       %4367 = OpBitcast %uint %4366
               OpBranch %4406
       %4406 = OpLabel
       %7279 = OpPhi %uint %4367 %4364 %4434 %4368 %4480 %4371 %4392 %4630 %4401 %4393 %4405 %4402
               OpSelectionMerge %4715 None
               OpBranchConditional %2056 %4707 %4709
       %4709 = OpLabel
       %4711 = OpIEqual %bool %1853 %uint_5
       %7498 = OpSelect %uint %4711 %uint_2 %uint_0
               OpBranch %4715
       %4707 = OpLabel
               OpBranch %4715
       %4715 = OpLabel
       %7340 = OpPhi %uint %1853 %4707 %7498 %4709
       %4724 = OpIAdd %uint %2072 %uint_5
       %4730 = OpCompositeConstruct %v2uint %4724 %2079
       %4733 = OpIAdd %v2uint %4730 %1791
       %4735 = OpShiftLeftLogical %v2uint %4733 %1264
       %4751 = OpCompositeConstruct %v2uint %7340 %7340
       %4744 = OpShiftRightLogical %v2uint %4751 %1065
       %4746 = OpBitwiseAnd %v2uint %4744 %7475
       %4738 = OpIAdd %v2uint %4735 %4746
       %4801 = OpCompositeExtract %uint %4738 0
       %4803 = OpUDiv %uint %4801 %2209
       %4805 = OpCompositeExtract %uint %4738 1
       %4807 = OpUDiv %uint %4805 %2214
       %4812 = OpIMul %uint %4803 %2209
       %4813 = OpISub %uint %4801 %4812
       %4818 = OpIMul %uint %4807 %2214
       %4819 = OpISub %uint %4805 %4818
       %4823 = OpIMul %uint %4807 %2172
       %4825 = OpIAdd %uint %4823 %4803
       %4829 = OpIAdd %uint %2177 %4825
       %4833 = OpISub %uint %4829 %2182
       %4838 = OpUDiv %uint %4833 %2185
       %4842 = OpIMul %uint %4838 %2185
       %4843 = OpISub %uint %4833 %4842
       %4846 = OpIMul %uint %4843 %2209
       %4848 = OpIAdd %uint %4846 %4813
       %4851 = OpIMul %uint %4838 %2214
       %4853 = OpIAdd %uint %4851 %4819
       %4869 = OpBitwiseAnd %uint %4853 %uint_1
       %4870 = OpINotEqual %bool %4869 %uint_0
               OpSelectionMerge %4877 None
               OpBranchConditional %4870 %4871 %4874
       %4874 = OpLabel
       %4875 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %4876 = OpLoad %uint %4875
               OpBranch %4877
       %4871 = OpLabel
       %4872 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %4873 = OpLoad %uint %4872
               OpBranch %4877
       %4877 = OpLabel
       %7342 = OpPhi %uint %4873 %4871 %4876 %4874
       %4768 = OpBitcast %int %4848
       %4771 = OpShiftRightLogical %uint %4853 %uint_1
       %4772 = OpBitcast %int %4771
       %4776 = OpCompositeConstruct %v2int %4768 %4772
       %4778 = OpBitcast %int %7342
       %4779 = OpImageFetch %v4float %2115 %4776 Sample %4778
               OpSelectionMerge %4936 None
               OpSwitch %1754 %4894 0 %4898 1 %4898 2 %4901 10 %4901 3 %4904 12 %4904 4 %4923 6 %4932
       %4932 = OpLabel
       %4934 = OpVectorShuffle %v2float %4779 %4779 0 1
       %4935 = OpExtInst %uint %1 PackHalf2x16 %4934
               OpBranch %4936
       %4923 = OpLabel
       %4925 = OpCompositeExtract %float %4779 0
       %5189 = OpExtInst %float %1 FMax %4925 %float_n1
       %5190 = OpExtInst %float %1 FMin %5189 %float_1
       %5192 = OpFOrdGreaterThanEqual %bool %5190 %float_0
       %5193 = OpSelect %float %5192 %float_0_5 %float_n0_5
       %5197 = OpExtInst %float %1 Fma %5190 %float_32767 %5193
       %5198 = OpConvertFToS %int %5197
       %5199 = OpBitcast %uint %5198
       %5200 = OpBitwiseAnd %uint %5199 %uint_65535
       %4928 = OpCompositeExtract %float %4779 1
       %5206 = OpExtInst %float %1 FMax %4928 %float_n1
       %5207 = OpExtInst %float %1 FMin %5206 %float_1
       %5209 = OpFOrdGreaterThanEqual %bool %5207 %float_0
       %5210 = OpSelect %float %5209 %float_0_5 %float_n0_5
       %5214 = OpExtInst %float %1 Fma %5207 %float_32767 %5210
       %5215 = OpConvertFToS %int %5214
       %5216 = OpBitcast %uint %5215
       %5217 = OpBitwiseAnd %uint %5216 %uint_65535
       %4930 = OpShiftLeftLogical %uint %5217 %uint_16
       %4931 = OpBitwiseOr %uint %5200 %4930
               OpBranch %4936
       %4904 = OpLabel
       %4906 = OpCompositeExtract %float %4779 0
       %5037 = OpExtInst %float %1 FMax %4906 %float_0
       %5038 = OpExtInst %float %1 FMin %5037 %float_31_875
       %5050 = OpBitcast %uint %5038
       %5052 = OpULessThan %bool %5050 %uint_1048576000
               OpSelectionMerge %5068 None
               OpBranchConditional %5052 %5053 %5065
       %5065 = OpLabel
       %5067 = OpIAdd %uint %5050 %uint_3254779904
               OpBranch %5068
       %5053 = OpLabel
       %5055 = OpShiftRightLogical %uint %5050 %uint_23
       %5057 = OpISub %uint %uint_125 %5055
       %5058 = OpExtInst %uint %1 UMin %5057 %uint_24
       %5060 = OpBitwiseAnd %uint %5050 %uint_8388607
       %5061 = OpBitwiseOr %uint %5060 %uint_8388608
       %5064 = OpShiftRightLogical %uint %5061 %5058
               OpBranch %5068
       %5068 = OpLabel
       %7343 = OpPhi %uint %5064 %5053 %5067 %5065
       %5070 = OpShiftRightLogical %uint %7343 %uint_16
       %5071 = OpBitwiseAnd %uint %5070 %uint_1
       %5073 = OpIAdd %uint %7343 %uint_32767
       %5075 = OpIAdd %uint %5073 %5071
       %5077 = OpShiftRightLogical %uint %5075 %uint_16
       %5078 = OpBitwiseAnd %uint %5077 %uint_1023
       %4909 = OpCompositeExtract %float %4779 1
       %5083 = OpExtInst %float %1 FMax %4909 %float_0
       %5084 = OpExtInst %float %1 FMin %5083 %float_31_875
       %5096 = OpBitcast %uint %5084
       %5098 = OpULessThan %bool %5096 %uint_1048576000
               OpSelectionMerge %5114 None
               OpBranchConditional %5098 %5099 %5111
       %5111 = OpLabel
       %5113 = OpIAdd %uint %5096 %uint_3254779904
               OpBranch %5114
       %5099 = OpLabel
       %5101 = OpShiftRightLogical %uint %5096 %uint_23
       %5103 = OpISub %uint %uint_125 %5101
       %5104 = OpExtInst %uint %1 UMin %5103 %uint_24
       %5106 = OpBitwiseAnd %uint %5096 %uint_8388607
       %5107 = OpBitwiseOr %uint %5106 %uint_8388608
       %5110 = OpShiftRightLogical %uint %5107 %5104
               OpBranch %5114
       %5114 = OpLabel
       %7344 = OpPhi %uint %5110 %5099 %5113 %5111
       %5116 = OpShiftRightLogical %uint %7344 %uint_16
       %5117 = OpBitwiseAnd %uint %5116 %uint_1
       %5119 = OpIAdd %uint %7344 %uint_32767
       %5121 = OpIAdd %uint %5119 %5117
       %5123 = OpShiftRightLogical %uint %5121 %uint_16
       %5124 = OpBitwiseAnd %uint %5123 %uint_1023
       %4911 = OpShiftLeftLogical %uint %5124 %uint_10
       %4912 = OpBitwiseOr %uint %5078 %4911
       %4914 = OpCompositeExtract %float %4779 2
       %5129 = OpExtInst %float %1 FMax %4914 %float_0
       %5130 = OpExtInst %float %1 FMin %5129 %float_31_875
       %5142 = OpBitcast %uint %5130
       %5144 = OpULessThan %bool %5142 %uint_1048576000
               OpSelectionMerge %5160 None
               OpBranchConditional %5144 %5145 %5157
       %5157 = OpLabel
       %5159 = OpIAdd %uint %5142 %uint_3254779904
               OpBranch %5160
       %5145 = OpLabel
       %5147 = OpShiftRightLogical %uint %5142 %uint_23
       %5149 = OpISub %uint %uint_125 %5147
       %5150 = OpExtInst %uint %1 UMin %5149 %uint_24
       %5152 = OpBitwiseAnd %uint %5142 %uint_8388607
       %5153 = OpBitwiseOr %uint %5152 %uint_8388608
       %5156 = OpShiftRightLogical %uint %5153 %5150
               OpBranch %5160
       %5160 = OpLabel
       %7345 = OpPhi %uint %5156 %5145 %5159 %5157
       %5162 = OpShiftRightLogical %uint %7345 %uint_16
       %5163 = OpBitwiseAnd %uint %5162 %uint_1
       %5165 = OpIAdd %uint %7345 %uint_32767
       %5167 = OpIAdd %uint %5165 %5163
       %5169 = OpShiftRightLogical %uint %5167 %uint_16
       %5170 = OpBitwiseAnd %uint %5169 %uint_1023
       %4916 = OpShiftLeftLogical %uint %5170 %uint_20
       %4917 = OpBitwiseOr %uint %4912 %4916
       %4919 = OpCompositeExtract %float %4779 3
       %5183 = OpExtInst %float %1 FClamp %4919 %float_0 %float_1
       %5178 = OpExtInst %float %1 Fma %5183 %float_3 %float_0_5
       %5179 = OpConvertFToU %uint %5178
       %4921 = OpShiftLeftLogical %uint %5179 %uint_30
       %4922 = OpBitwiseOr %uint %4917 %4921
               OpBranch %4936
       %4901 = OpLabel
       %5018 = OpExtInst %v4float %1 FClamp %4779 %7479 %7480
       %4995 = OpExtInst %v4float %1 Fma %5018 %297 %7481
       %4996 = OpConvertFToU %v4uint %4995
       %4998 = OpCompositeExtract %uint %4996 0
       %5000 = OpCompositeExtract %uint %4996 1
       %5001 = OpShiftLeftLogical %uint %5000 %int_10
       %5002 = OpBitwiseOr %uint %4998 %5001
       %5004 = OpCompositeExtract %uint %4996 2
       %5005 = OpShiftLeftLogical %uint %5004 %int_20
       %5006 = OpBitwiseOr %uint %5002 %5005
       %5008 = OpCompositeExtract %uint %4996 3
       %5009 = OpShiftLeftLogical %uint %5008 %int_30
       %5010 = OpBitwiseOr %uint %5006 %5009
               OpBranch %4936
       %4898 = OpLabel
       %4972 = OpExtInst %v4float %1 FClamp %4779 %7479 %7480
       %4947 = OpVectorTimesScalar %v4float %4972 %float_255
       %4949 = OpFAdd %v4float %4947 %7481
       %4950 = OpConvertFToU %v4uint %4949
       %4952 = OpCompositeExtract %uint %4950 0
       %4954 = OpCompositeExtract %uint %4950 1
       %4955 = OpShiftLeftLogical %uint %4954 %int_8
       %4956 = OpBitwiseOr %uint %4952 %4955
       %4958 = OpCompositeExtract %uint %4950 2
       %4959 = OpShiftLeftLogical %uint %4958 %int_16
       %4960 = OpBitwiseOr %uint %4956 %4959
       %4962 = OpCompositeExtract %uint %4950 3
       %4963 = OpShiftLeftLogical %uint %4962 %int_24
       %4964 = OpBitwiseOr %uint %4960 %4963
               OpBranch %4936
       %4894 = OpLabel
       %4896 = OpCompositeExtract %float %4779 0
       %4897 = OpBitcast %uint %4896
               OpBranch %4936
       %4936 = OpLabel
       %7348 = OpPhi %uint %4897 %4894 %4964 %4898 %5010 %4901 %4922 %5160 %4931 %4923 %4935 %4932
               OpSelectionMerge %5245 None
               OpBranchConditional %2056 %5237 %5239
       %5239 = OpLabel
       %5241 = OpIEqual %bool %1853 %uint_5
       %7499 = OpSelect %uint %5241 %uint_2 %uint_0
               OpBranch %5245
       %5237 = OpLabel
               OpBranch %5245
       %5245 = OpLabel
       %7360 = OpPhi %uint %1853 %5237 %7499 %5239
       %5254 = OpIAdd %uint %2072 %uint_6
       %5260 = OpCompositeConstruct %v2uint %5254 %2079
       %5263 = OpIAdd %v2uint %5260 %1791
       %5265 = OpShiftLeftLogical %v2uint %5263 %1264
       %5281 = OpCompositeConstruct %v2uint %7360 %7360
       %5274 = OpShiftRightLogical %v2uint %5281 %1065
       %5276 = OpBitwiseAnd %v2uint %5274 %7475
       %5268 = OpIAdd %v2uint %5265 %5276
       %5331 = OpCompositeExtract %uint %5268 0
       %5333 = OpUDiv %uint %5331 %2209
       %5335 = OpCompositeExtract %uint %5268 1
       %5337 = OpUDiv %uint %5335 %2214
       %5342 = OpIMul %uint %5333 %2209
       %5343 = OpISub %uint %5331 %5342
       %5348 = OpIMul %uint %5337 %2214
       %5349 = OpISub %uint %5335 %5348
       %5353 = OpIMul %uint %5337 %2172
       %5355 = OpIAdd %uint %5353 %5333
       %5359 = OpIAdd %uint %2177 %5355
       %5363 = OpISub %uint %5359 %2182
       %5368 = OpUDiv %uint %5363 %2185
       %5372 = OpIMul %uint %5368 %2185
       %5373 = OpISub %uint %5363 %5372
       %5376 = OpIMul %uint %5373 %2209
       %5378 = OpIAdd %uint %5376 %5343
       %5381 = OpIMul %uint %5368 %2214
       %5383 = OpIAdd %uint %5381 %5349
       %5399 = OpBitwiseAnd %uint %5383 %uint_1
       %5400 = OpINotEqual %bool %5399 %uint_0
               OpSelectionMerge %5407 None
               OpBranchConditional %5400 %5401 %5404
       %5404 = OpLabel
       %5405 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %5406 = OpLoad %uint %5405
               OpBranch %5407
       %5401 = OpLabel
       %5402 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %5403 = OpLoad %uint %5402
               OpBranch %5407
       %5407 = OpLabel
       %7362 = OpPhi %uint %5403 %5401 %5406 %5404
       %5298 = OpBitcast %int %5378
       %5301 = OpShiftRightLogical %uint %5383 %uint_1
       %5302 = OpBitcast %int %5301
       %5306 = OpCompositeConstruct %v2int %5298 %5302
       %5308 = OpBitcast %int %7362
       %5309 = OpImageFetch %v4float %2115 %5306 Sample %5308
               OpSelectionMerge %5466 None
               OpSwitch %1754 %5424 0 %5428 1 %5428 2 %5431 10 %5431 3 %5434 12 %5434 4 %5453 6 %5462
       %5462 = OpLabel
       %5464 = OpVectorShuffle %v2float %5309 %5309 0 1
       %5465 = OpExtInst %uint %1 PackHalf2x16 %5464
               OpBranch %5466
       %5453 = OpLabel
       %5455 = OpCompositeExtract %float %5309 0
       %5719 = OpExtInst %float %1 FMax %5455 %float_n1
       %5720 = OpExtInst %float %1 FMin %5719 %float_1
       %5722 = OpFOrdGreaterThanEqual %bool %5720 %float_0
       %5723 = OpSelect %float %5722 %float_0_5 %float_n0_5
       %5727 = OpExtInst %float %1 Fma %5720 %float_32767 %5723
       %5728 = OpConvertFToS %int %5727
       %5729 = OpBitcast %uint %5728
       %5730 = OpBitwiseAnd %uint %5729 %uint_65535
       %5458 = OpCompositeExtract %float %5309 1
       %5736 = OpExtInst %float %1 FMax %5458 %float_n1
       %5737 = OpExtInst %float %1 FMin %5736 %float_1
       %5739 = OpFOrdGreaterThanEqual %bool %5737 %float_0
       %5740 = OpSelect %float %5739 %float_0_5 %float_n0_5
       %5744 = OpExtInst %float %1 Fma %5737 %float_32767 %5740
       %5745 = OpConvertFToS %int %5744
       %5746 = OpBitcast %uint %5745
       %5747 = OpBitwiseAnd %uint %5746 %uint_65535
       %5460 = OpShiftLeftLogical %uint %5747 %uint_16
       %5461 = OpBitwiseOr %uint %5730 %5460
               OpBranch %5466
       %5434 = OpLabel
       %5436 = OpCompositeExtract %float %5309 0
       %5567 = OpExtInst %float %1 FMax %5436 %float_0
       %5568 = OpExtInst %float %1 FMin %5567 %float_31_875
       %5580 = OpBitcast %uint %5568
       %5582 = OpULessThan %bool %5580 %uint_1048576000
               OpSelectionMerge %5598 None
               OpBranchConditional %5582 %5583 %5595
       %5595 = OpLabel
       %5597 = OpIAdd %uint %5580 %uint_3254779904
               OpBranch %5598
       %5583 = OpLabel
       %5585 = OpShiftRightLogical %uint %5580 %uint_23
       %5587 = OpISub %uint %uint_125 %5585
       %5588 = OpExtInst %uint %1 UMin %5587 %uint_24
       %5590 = OpBitwiseAnd %uint %5580 %uint_8388607
       %5591 = OpBitwiseOr %uint %5590 %uint_8388608
       %5594 = OpShiftRightLogical %uint %5591 %5588
               OpBranch %5598
       %5598 = OpLabel
       %7363 = OpPhi %uint %5594 %5583 %5597 %5595
       %5600 = OpShiftRightLogical %uint %7363 %uint_16
       %5601 = OpBitwiseAnd %uint %5600 %uint_1
       %5603 = OpIAdd %uint %7363 %uint_32767
       %5605 = OpIAdd %uint %5603 %5601
       %5607 = OpShiftRightLogical %uint %5605 %uint_16
       %5608 = OpBitwiseAnd %uint %5607 %uint_1023
       %5439 = OpCompositeExtract %float %5309 1
       %5613 = OpExtInst %float %1 FMax %5439 %float_0
       %5614 = OpExtInst %float %1 FMin %5613 %float_31_875
       %5626 = OpBitcast %uint %5614
       %5628 = OpULessThan %bool %5626 %uint_1048576000
               OpSelectionMerge %5644 None
               OpBranchConditional %5628 %5629 %5641
       %5641 = OpLabel
       %5643 = OpIAdd %uint %5626 %uint_3254779904
               OpBranch %5644
       %5629 = OpLabel
       %5631 = OpShiftRightLogical %uint %5626 %uint_23
       %5633 = OpISub %uint %uint_125 %5631
       %5634 = OpExtInst %uint %1 UMin %5633 %uint_24
       %5636 = OpBitwiseAnd %uint %5626 %uint_8388607
       %5637 = OpBitwiseOr %uint %5636 %uint_8388608
       %5640 = OpShiftRightLogical %uint %5637 %5634
               OpBranch %5644
       %5644 = OpLabel
       %7364 = OpPhi %uint %5640 %5629 %5643 %5641
       %5646 = OpShiftRightLogical %uint %7364 %uint_16
       %5647 = OpBitwiseAnd %uint %5646 %uint_1
       %5649 = OpIAdd %uint %7364 %uint_32767
       %5651 = OpIAdd %uint %5649 %5647
       %5653 = OpShiftRightLogical %uint %5651 %uint_16
       %5654 = OpBitwiseAnd %uint %5653 %uint_1023
       %5441 = OpShiftLeftLogical %uint %5654 %uint_10
       %5442 = OpBitwiseOr %uint %5608 %5441
       %5444 = OpCompositeExtract %float %5309 2
       %5659 = OpExtInst %float %1 FMax %5444 %float_0
       %5660 = OpExtInst %float %1 FMin %5659 %float_31_875
       %5672 = OpBitcast %uint %5660
       %5674 = OpULessThan %bool %5672 %uint_1048576000
               OpSelectionMerge %5690 None
               OpBranchConditional %5674 %5675 %5687
       %5687 = OpLabel
       %5689 = OpIAdd %uint %5672 %uint_3254779904
               OpBranch %5690
       %5675 = OpLabel
       %5677 = OpShiftRightLogical %uint %5672 %uint_23
       %5679 = OpISub %uint %uint_125 %5677
       %5680 = OpExtInst %uint %1 UMin %5679 %uint_24
       %5682 = OpBitwiseAnd %uint %5672 %uint_8388607
       %5683 = OpBitwiseOr %uint %5682 %uint_8388608
       %5686 = OpShiftRightLogical %uint %5683 %5680
               OpBranch %5690
       %5690 = OpLabel
       %7365 = OpPhi %uint %5686 %5675 %5689 %5687
       %5692 = OpShiftRightLogical %uint %7365 %uint_16
       %5693 = OpBitwiseAnd %uint %5692 %uint_1
       %5695 = OpIAdd %uint %7365 %uint_32767
       %5697 = OpIAdd %uint %5695 %5693
       %5699 = OpShiftRightLogical %uint %5697 %uint_16
       %5700 = OpBitwiseAnd %uint %5699 %uint_1023
       %5446 = OpShiftLeftLogical %uint %5700 %uint_20
       %5447 = OpBitwiseOr %uint %5442 %5446
       %5449 = OpCompositeExtract %float %5309 3
       %5713 = OpExtInst %float %1 FClamp %5449 %float_0 %float_1
       %5708 = OpExtInst %float %1 Fma %5713 %float_3 %float_0_5
       %5709 = OpConvertFToU %uint %5708
       %5451 = OpShiftLeftLogical %uint %5709 %uint_30
       %5452 = OpBitwiseOr %uint %5447 %5451
               OpBranch %5466
       %5431 = OpLabel
       %5548 = OpExtInst %v4float %1 FClamp %5309 %7479 %7480
       %5525 = OpExtInst %v4float %1 Fma %5548 %297 %7481
       %5526 = OpConvertFToU %v4uint %5525
       %5528 = OpCompositeExtract %uint %5526 0
       %5530 = OpCompositeExtract %uint %5526 1
       %5531 = OpShiftLeftLogical %uint %5530 %int_10
       %5532 = OpBitwiseOr %uint %5528 %5531
       %5534 = OpCompositeExtract %uint %5526 2
       %5535 = OpShiftLeftLogical %uint %5534 %int_20
       %5536 = OpBitwiseOr %uint %5532 %5535
       %5538 = OpCompositeExtract %uint %5526 3
       %5539 = OpShiftLeftLogical %uint %5538 %int_30
       %5540 = OpBitwiseOr %uint %5536 %5539
               OpBranch %5466
       %5428 = OpLabel
       %5502 = OpExtInst %v4float %1 FClamp %5309 %7479 %7480
       %5477 = OpVectorTimesScalar %v4float %5502 %float_255
       %5479 = OpFAdd %v4float %5477 %7481
       %5480 = OpConvertFToU %v4uint %5479
       %5482 = OpCompositeExtract %uint %5480 0
       %5484 = OpCompositeExtract %uint %5480 1
       %5485 = OpShiftLeftLogical %uint %5484 %int_8
       %5486 = OpBitwiseOr %uint %5482 %5485
       %5488 = OpCompositeExtract %uint %5480 2
       %5489 = OpShiftLeftLogical %uint %5488 %int_16
       %5490 = OpBitwiseOr %uint %5486 %5489
       %5492 = OpCompositeExtract %uint %5480 3
       %5493 = OpShiftLeftLogical %uint %5492 %int_24
       %5494 = OpBitwiseOr %uint %5490 %5493
               OpBranch %5466
       %5424 = OpLabel
       %5426 = OpCompositeExtract %float %5309 0
       %5427 = OpBitcast %uint %5426
               OpBranch %5466
       %5466 = OpLabel
       %7368 = OpPhi %uint %5427 %5424 %5494 %5428 %5540 %5431 %5452 %5690 %5461 %5453 %5465 %5462
               OpSelectionMerge %5775 None
               OpBranchConditional %2056 %5767 %5769
       %5769 = OpLabel
       %5771 = OpIEqual %bool %1853 %uint_5
       %7500 = OpSelect %uint %5771 %uint_2 %uint_0
               OpBranch %5775
       %5767 = OpLabel
               OpBranch %5775
       %5775 = OpLabel
       %7380 = OpPhi %uint %1853 %5767 %7500 %5769
       %5784 = OpIAdd %uint %2072 %uint_7
       %5790 = OpCompositeConstruct %v2uint %5784 %2079
       %5793 = OpIAdd %v2uint %5790 %1791
       %5795 = OpShiftLeftLogical %v2uint %5793 %1264
       %5811 = OpCompositeConstruct %v2uint %7380 %7380
       %5804 = OpShiftRightLogical %v2uint %5811 %1065
       %5806 = OpBitwiseAnd %v2uint %5804 %7475
       %5798 = OpIAdd %v2uint %5795 %5806
       %5861 = OpCompositeExtract %uint %5798 0
       %5863 = OpUDiv %uint %5861 %2209
       %5865 = OpCompositeExtract %uint %5798 1
       %5867 = OpUDiv %uint %5865 %2214
       %5872 = OpIMul %uint %5863 %2209
       %5873 = OpISub %uint %5861 %5872
       %5878 = OpIMul %uint %5867 %2214
       %5879 = OpISub %uint %5865 %5878
       %5883 = OpIMul %uint %5867 %2172
       %5885 = OpIAdd %uint %5883 %5863
       %5889 = OpIAdd %uint %2177 %5885
       %5893 = OpISub %uint %5889 %2182
       %5898 = OpUDiv %uint %5893 %2185
       %5902 = OpIMul %uint %5898 %2185
       %5903 = OpISub %uint %5893 %5902
       %5906 = OpIMul %uint %5903 %2209
       %5908 = OpIAdd %uint %5906 %5873
       %5911 = OpIMul %uint %5898 %2214
       %5913 = OpIAdd %uint %5911 %5879
       %5929 = OpBitwiseAnd %uint %5913 %uint_1
       %5930 = OpINotEqual %bool %5929 %uint_0
               OpSelectionMerge %5937 None
               OpBranchConditional %5930 %5931 %5934
       %5934 = OpLabel
       %5935 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %5936 = OpLoad %uint %5935
               OpBranch %5937
       %5931 = OpLabel
       %5932 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %5933 = OpLoad %uint %5932
               OpBranch %5937
       %5937 = OpLabel
       %7382 = OpPhi %uint %5933 %5931 %5936 %5934
       %5828 = OpBitcast %int %5908
       %5831 = OpShiftRightLogical %uint %5913 %uint_1
       %5832 = OpBitcast %int %5831
       %5836 = OpCompositeConstruct %v2int %5828 %5832
       %5838 = OpBitcast %int %7382
       %5839 = OpImageFetch %v4float %2115 %5836 Sample %5838
               OpSelectionMerge %5996 None
               OpSwitch %1754 %5954 0 %5958 1 %5958 2 %5961 10 %5961 3 %5964 12 %5964 4 %5983 6 %5992
       %5992 = OpLabel
       %5994 = OpVectorShuffle %v2float %5839 %5839 0 1
       %5995 = OpExtInst %uint %1 PackHalf2x16 %5994
               OpBranch %5996
       %5983 = OpLabel
       %5985 = OpCompositeExtract %float %5839 0
       %6249 = OpExtInst %float %1 FMax %5985 %float_n1
       %6250 = OpExtInst %float %1 FMin %6249 %float_1
       %6252 = OpFOrdGreaterThanEqual %bool %6250 %float_0
       %6253 = OpSelect %float %6252 %float_0_5 %float_n0_5
       %6257 = OpExtInst %float %1 Fma %6250 %float_32767 %6253
       %6258 = OpConvertFToS %int %6257
       %6259 = OpBitcast %uint %6258
       %6260 = OpBitwiseAnd %uint %6259 %uint_65535
       %5988 = OpCompositeExtract %float %5839 1
       %6266 = OpExtInst %float %1 FMax %5988 %float_n1
       %6267 = OpExtInst %float %1 FMin %6266 %float_1
       %6269 = OpFOrdGreaterThanEqual %bool %6267 %float_0
       %6270 = OpSelect %float %6269 %float_0_5 %float_n0_5
       %6274 = OpExtInst %float %1 Fma %6267 %float_32767 %6270
       %6275 = OpConvertFToS %int %6274
       %6276 = OpBitcast %uint %6275
       %6277 = OpBitwiseAnd %uint %6276 %uint_65535
       %5990 = OpShiftLeftLogical %uint %6277 %uint_16
       %5991 = OpBitwiseOr %uint %6260 %5990
               OpBranch %5996
       %5964 = OpLabel
       %5966 = OpCompositeExtract %float %5839 0
       %6097 = OpExtInst %float %1 FMax %5966 %float_0
       %6098 = OpExtInst %float %1 FMin %6097 %float_31_875
       %6110 = OpBitcast %uint %6098
       %6112 = OpULessThan %bool %6110 %uint_1048576000
               OpSelectionMerge %6128 None
               OpBranchConditional %6112 %6113 %6125
       %6125 = OpLabel
       %6127 = OpIAdd %uint %6110 %uint_3254779904
               OpBranch %6128
       %6113 = OpLabel
       %6115 = OpShiftRightLogical %uint %6110 %uint_23
       %6117 = OpISub %uint %uint_125 %6115
       %6118 = OpExtInst %uint %1 UMin %6117 %uint_24
       %6120 = OpBitwiseAnd %uint %6110 %uint_8388607
       %6121 = OpBitwiseOr %uint %6120 %uint_8388608
       %6124 = OpShiftRightLogical %uint %6121 %6118
               OpBranch %6128
       %6128 = OpLabel
       %7383 = OpPhi %uint %6124 %6113 %6127 %6125
       %6130 = OpShiftRightLogical %uint %7383 %uint_16
       %6131 = OpBitwiseAnd %uint %6130 %uint_1
       %6133 = OpIAdd %uint %7383 %uint_32767
       %6135 = OpIAdd %uint %6133 %6131
       %6137 = OpShiftRightLogical %uint %6135 %uint_16
       %6138 = OpBitwiseAnd %uint %6137 %uint_1023
       %5969 = OpCompositeExtract %float %5839 1
       %6143 = OpExtInst %float %1 FMax %5969 %float_0
       %6144 = OpExtInst %float %1 FMin %6143 %float_31_875
       %6156 = OpBitcast %uint %6144
       %6158 = OpULessThan %bool %6156 %uint_1048576000
               OpSelectionMerge %6174 None
               OpBranchConditional %6158 %6159 %6171
       %6171 = OpLabel
       %6173 = OpIAdd %uint %6156 %uint_3254779904
               OpBranch %6174
       %6159 = OpLabel
       %6161 = OpShiftRightLogical %uint %6156 %uint_23
       %6163 = OpISub %uint %uint_125 %6161
       %6164 = OpExtInst %uint %1 UMin %6163 %uint_24
       %6166 = OpBitwiseAnd %uint %6156 %uint_8388607
       %6167 = OpBitwiseOr %uint %6166 %uint_8388608
       %6170 = OpShiftRightLogical %uint %6167 %6164
               OpBranch %6174
       %6174 = OpLabel
       %7384 = OpPhi %uint %6170 %6159 %6173 %6171
       %6176 = OpShiftRightLogical %uint %7384 %uint_16
       %6177 = OpBitwiseAnd %uint %6176 %uint_1
       %6179 = OpIAdd %uint %7384 %uint_32767
       %6181 = OpIAdd %uint %6179 %6177
       %6183 = OpShiftRightLogical %uint %6181 %uint_16
       %6184 = OpBitwiseAnd %uint %6183 %uint_1023
       %5971 = OpShiftLeftLogical %uint %6184 %uint_10
       %5972 = OpBitwiseOr %uint %6138 %5971
       %5974 = OpCompositeExtract %float %5839 2
       %6189 = OpExtInst %float %1 FMax %5974 %float_0
       %6190 = OpExtInst %float %1 FMin %6189 %float_31_875
       %6202 = OpBitcast %uint %6190
       %6204 = OpULessThan %bool %6202 %uint_1048576000
               OpSelectionMerge %6220 None
               OpBranchConditional %6204 %6205 %6217
       %6217 = OpLabel
       %6219 = OpIAdd %uint %6202 %uint_3254779904
               OpBranch %6220
       %6205 = OpLabel
       %6207 = OpShiftRightLogical %uint %6202 %uint_23
       %6209 = OpISub %uint %uint_125 %6207
       %6210 = OpExtInst %uint %1 UMin %6209 %uint_24
       %6212 = OpBitwiseAnd %uint %6202 %uint_8388607
       %6213 = OpBitwiseOr %uint %6212 %uint_8388608
       %6216 = OpShiftRightLogical %uint %6213 %6210
               OpBranch %6220
       %6220 = OpLabel
       %7385 = OpPhi %uint %6216 %6205 %6219 %6217
       %6222 = OpShiftRightLogical %uint %7385 %uint_16
       %6223 = OpBitwiseAnd %uint %6222 %uint_1
       %6225 = OpIAdd %uint %7385 %uint_32767
       %6227 = OpIAdd %uint %6225 %6223
       %6229 = OpShiftRightLogical %uint %6227 %uint_16
       %6230 = OpBitwiseAnd %uint %6229 %uint_1023
       %5976 = OpShiftLeftLogical %uint %6230 %uint_20
       %5977 = OpBitwiseOr %uint %5972 %5976
       %5979 = OpCompositeExtract %float %5839 3
       %6243 = OpExtInst %float %1 FClamp %5979 %float_0 %float_1
       %6238 = OpExtInst %float %1 Fma %6243 %float_3 %float_0_5
       %6239 = OpConvertFToU %uint %6238
       %5981 = OpShiftLeftLogical %uint %6239 %uint_30
       %5982 = OpBitwiseOr %uint %5977 %5981
               OpBranch %5996
       %5961 = OpLabel
       %6078 = OpExtInst %v4float %1 FClamp %5839 %7479 %7480
       %6055 = OpExtInst %v4float %1 Fma %6078 %297 %7481
       %6056 = OpConvertFToU %v4uint %6055
       %6058 = OpCompositeExtract %uint %6056 0
       %6060 = OpCompositeExtract %uint %6056 1
       %6061 = OpShiftLeftLogical %uint %6060 %int_10
       %6062 = OpBitwiseOr %uint %6058 %6061
       %6064 = OpCompositeExtract %uint %6056 2
       %6065 = OpShiftLeftLogical %uint %6064 %int_20
       %6066 = OpBitwiseOr %uint %6062 %6065
       %6068 = OpCompositeExtract %uint %6056 3
       %6069 = OpShiftLeftLogical %uint %6068 %int_30
       %6070 = OpBitwiseOr %uint %6066 %6069
               OpBranch %5996
       %5958 = OpLabel
       %6032 = OpExtInst %v4float %1 FClamp %5839 %7479 %7480
       %6007 = OpVectorTimesScalar %v4float %6032 %float_255
       %6009 = OpFAdd %v4float %6007 %7481
       %6010 = OpConvertFToU %v4uint %6009
       %6012 = OpCompositeExtract %uint %6010 0
       %6014 = OpCompositeExtract %uint %6010 1
       %6015 = OpShiftLeftLogical %uint %6014 %int_8
       %6016 = OpBitwiseOr %uint %6012 %6015
       %6018 = OpCompositeExtract %uint %6010 2
       %6019 = OpShiftLeftLogical %uint %6018 %int_16
       %6020 = OpBitwiseOr %uint %6016 %6019
       %6022 = OpCompositeExtract %uint %6010 3
       %6023 = OpShiftLeftLogical %uint %6022 %int_24
       %6024 = OpBitwiseOr %uint %6020 %6023
               OpBranch %5996
       %5954 = OpLabel
       %5956 = OpCompositeExtract %float %5839 0
       %5957 = OpBitcast %uint %5956
               OpBranch %5996
       %5996 = OpLabel
       %7388 = OpPhi %uint %5957 %5954 %6024 %5958 %6070 %5961 %5982 %6220 %5991 %5983 %5995 %5992
       %7492 = OpCompositeConstruct %v4uint %7279 %7348 %7368 %7388
       %6281 = OpIEqual %bool %2072 %uint_0
               OpSelectionMerge %6286 None
               OpBranchConditional %6281 %6282 %6286
       %6282 = OpLabel
       %6284 = OpCompositeExtract %uint %7177 0
       %6285 = OpINotEqual %bool %6284 %uint_0
               OpBranch %6286
       %6286 = OpLabel
       %6287 = OpPhi %bool %6281 %5996 %6285 %6282
               OpSelectionMerge %6308 DontFlatten
               OpBranchConditional %6287 %6288 %6308
       %6288 = OpLabel
       %6290 = OpCompositeExtract %uint %7177 0
       %6291 = OpUGreaterThanEqual %bool %6290 %uint_2
               OpSelectionMerge %6304 None
               OpBranchConditional %6291 %6292 %6304
       %6292 = OpLabel
       %6295 = OpUGreaterThanEqual %bool %6290 %uint_3
               OpSelectionMerge %6300 None
               OpBranchConditional %6295 %6296 %6300
       %6296 = OpLabel
       %7164 = OpCompositeInsert %v4uint %7259 %7491 2
               OpBranch %6300
       %6300 = OpLabel
       %7445 = OpPhi %v4uint %7491 %6292 %7164 %6296
       %6302 = OpCompositeExtract %uint %7445 2
       %7167 = OpCompositeInsert %v4uint %6302 %7445 1
               OpBranch %6304
       %6304 = OpLabel
       %7446 = OpPhi %v4uint %7491 %6288 %7167 %6300
       %6306 = OpCompositeExtract %uint %7446 1
       %7170 = OpCompositeInsert %v4uint %6306 %7446 0
               OpBranch %6308
       %6308 = OpLabel
       %7447 = OpPhi %v4uint %7491 %6286 %7170 %6304
               OpSelectionMerge %6330 DontFlatten
               OpBranchConditional %1830 %6316 %6330
       %6316 = OpLabel
               OpSelectionMerge %6329 None
               OpSwitch %1754 %6329 0 %6319 1 %6319 2 %6324 3 %6324 10 %6324 12 %6324
       %6324 = OpLabel
       %6369 = OpBitwiseAnd %v4uint %7447 %7485
       %6372 = OpBitwiseAnd %v4uint %7447 %7486
       %6374 = OpShiftLeftLogical %v4uint %6372 %7487
       %6375 = OpBitwiseOr %v4uint %6369 %6374
       %6378 = OpShiftRightLogical %v4uint %7447 %7487
       %6380 = OpBitwiseAnd %v4uint %6378 %7486
       %6381 = OpBitwiseOr %v4uint %6375 %6380
       %6386 = OpBitwiseAnd %v4uint %7492 %7485
       %6389 = OpBitwiseAnd %v4uint %7492 %7486
       %6391 = OpShiftLeftLogical %v4uint %6389 %7487
       %6392 = OpBitwiseOr %v4uint %6386 %6391
       %6395 = OpShiftRightLogical %v4uint %7492 %7487
       %6397 = OpBitwiseAnd %v4uint %6395 %7486
       %6398 = OpBitwiseOr %v4uint %6392 %6397
               OpBranch %6329
       %6319 = OpLabel
       %6335 = OpBitwiseAnd %v4uint %7447 %7482
       %6338 = OpBitwiseAnd %v4uint %7447 %7483
       %6340 = OpShiftLeftLogical %v4uint %6338 %7484
       %6341 = OpBitwiseOr %v4uint %6335 %6340
       %6344 = OpShiftRightLogical %v4uint %7447 %7484
       %6346 = OpBitwiseAnd %v4uint %6344 %7483
       %6347 = OpBitwiseOr %v4uint %6341 %6346
       %6352 = OpBitwiseAnd %v4uint %7492 %7482
       %6355 = OpBitwiseAnd %v4uint %7492 %7483
       %6357 = OpShiftLeftLogical %v4uint %6355 %7484
       %6358 = OpBitwiseOr %v4uint %6352 %6357
       %6361 = OpShiftRightLogical %v4uint %7492 %7484
       %6363 = OpBitwiseAnd %v4uint %6361 %7483
       %6364 = OpBitwiseOr %v4uint %6358 %6363
               OpBranch %6329
       %6329 = OpLabel
       %7459 = OpPhi %v4uint %7492 %6316 %6364 %6319 %6398 %6324
       %7457 = OpPhi %v4uint %7447 %6316 %6347 %6319 %6381 %6324
               OpBranch %6330
       %6330 = OpLabel
       %7458 = OpPhi %v4uint %7492 %6308 %7459 %6329
       %7456 = OpPhi %v4uint %7447 %6308 %7457 %6329
       %6406 = OpIAdd %v2uint %7179 %1849
       %6457 = OpShiftRightLogical %v2uint %6406 %596
       %6459 = OpUDiv %v2uint %6457 %1764
       %6462 = OpIMul %v2uint %1764 %6459
       %6463 = OpISub %v2uint %6457 %6462
       %6466 = OpShiftLeftLogical %v2uint %6459 %596
       %6469 = OpCompositeExtract %uint %6463 0
       %6471 = OpIMul %uint %6469 %2213
       %6473 = OpCompositeExtract %uint %6463 1
       %6474 = OpIAdd %uint %6471 %6473
       %6483 = OpBitwiseAnd %v2uint %6406 %1785
       %6489 = OpShiftLeftLogical %uint %6474 %uint_7
       %6491 = OpCompositeExtract %uint %6483 1
       %6493 = OpShiftLeftLogical %uint %6491 %uint_6
       %6494 = OpBitwiseOr %uint %6489 %6493
       %6496 = OpCompositeExtract %uint %6483 0
       %6497 = OpShiftLeftLogical %uint %6496 %uint_2
       %6498 = OpBitwiseOr %uint %6494 %6497
               OpSelectionMerge %6431 DontFlatten
               OpBranchConditional %1809 %6414 %6425
       %6425 = OpLabel
       %6427 = OpBitcast %v2int %6466
       %6598 = OpCompositeExtract %int %6427 1
       %6599 = OpShiftRightArithmetic %int %6598 %int_5
       %6600 = OpBitcast %int %1833
       %6601 = OpIMul %int %6599 %6600
       %6602 = OpCompositeExtract %int %6427 0
       %6603 = OpShiftRightArithmetic %int %6602 %int_5
       %6604 = OpIAdd %int %6601 %6603
       %6605 = OpShiftLeftLogical %int %6604 %int_6
       %6607 = OpShiftRightArithmetic %int %6598 %int_1
       %6608 = OpBitwiseAnd %int %6607 %int_7
       %6609 = OpShiftLeftLogical %int %6608 %int_3
       %6611 = OpBitwiseAnd %int %6602 %int_7
       %6612 = OpBitwiseOr %int %6609 %6611
       %6615 = OpBitwiseOr %int %6605 %6612
       %6616 = OpShiftLeftLogical %int %6615 %uint_2
       %6618 = OpShiftRightArithmetic %int %6598 %int_4
       %6619 = OpBitwiseAnd %int %6618 %int_1
       %6621 = OpShiftRightArithmetic %int %6602 %int_3
       %6622 = OpBitwiseAnd %int %6621 %int_3
       %6624 = OpShiftRightArithmetic %int %6598 %int_3
       %6625 = OpBitwiseAnd %int %6624 %int_1
       %6626 = OpShiftLeftLogical %int %6625 %int_1
       %6627 = OpBitwiseXor %int %6622 %6626
       %6632 = OpBitwiseAnd %int %6598 %int_1
       %6636 = OpShiftLeftLogical %int %6632 %int_4
       %6637 = OpShiftLeftLogical %int %6627 %int_6
       %6638 = OpBitwiseOr %int %6636 %6637
       %6639 = OpShiftLeftLogical %int %6619 %int_11
       %6640 = OpBitwiseOr %int %6638 %6639
       %6641 = OpBitwiseAnd %int %6616 %int_15
       %6642 = OpBitwiseOr %int %6640 %6641
       %6643 = OpShiftRightArithmetic %int %6616 %int_4
       %6644 = OpBitwiseAnd %int %6643 %int_1
       %6645 = OpShiftLeftLogical %int %6644 %int_5
       %6646 = OpBitwiseOr %int %6642 %6645
       %6647 = OpShiftRightArithmetic %int %6616 %int_5
       %6648 = OpBitwiseAnd %int %6647 %int_7
       %6649 = OpShiftLeftLogical %int %6648 %int_8
       %6650 = OpBitwiseOr %int %6646 %6649
       %6651 = OpShiftRightArithmetic %int %6616 %int_8
       %6652 = OpShiftLeftLogical %int %6651 %int_12
       %6653 = OpBitwiseOr %int %6650 %6652
       %6430 = OpBitcast %uint %6653
               OpBranch %6431
       %6414 = OpLabel
       %6417 = OpCompositeExtract %uint %6466 0
       %6418 = OpCompositeExtract %uint %6466 1
       %6419 = OpCompositeConstruct %v3uint %6417 %6418 %1813
       %6420 = OpBitcast %v3int %6419
       %6525 = OpCompositeExtract %int %6420 2
       %6526 = OpShiftRightArithmetic %int %6525 %int_2
       %6527 = OpBitcast %int %1838
       %6528 = OpIMul %int %6526 %6527
       %6529 = OpCompositeExtract %int %6420 1
       %6530 = OpShiftRightArithmetic %int %6529 %int_4
       %6531 = OpIAdd %int %6528 %6530
       %6532 = OpBitcast %int %1833
       %6533 = OpIMul %int %6531 %6532
       %6534 = OpCompositeExtract %int %6420 0
       %6535 = OpShiftRightArithmetic %int %6534 %int_5
       %6536 = OpIAdd %int %6533 %6535
       %6537 = OpShiftLeftLogical %int %6536 %int_7
       %6539 = OpBitwiseAnd %int %6525 %int_3
       %6540 = OpShiftLeftLogical %int %6539 %int_5
       %6542 = OpShiftRightArithmetic %int %6529 %int_1
       %6543 = OpBitwiseAnd %int %6542 %int_3
       %6544 = OpShiftLeftLogical %int %6543 %int_3
       %6545 = OpBitwiseOr %int %6540 %6544
       %6547 = OpBitwiseAnd %int %6534 %int_7
       %6548 = OpBitwiseOr %int %6545 %6547
       %6551 = OpBitwiseOr %int %6537 %6548
       %6552 = OpShiftLeftLogical %int %6551 %uint_2
       %6554 = OpShiftRightArithmetic %int %6529 %int_3
       %6557 = OpBitwiseXor %int %6554 %6526
       %6558 = OpBitwiseAnd %int %6557 %int_1
       %6560 = OpShiftRightArithmetic %int %6534 %int_3
       %6561 = OpBitwiseAnd %int %6560 %int_3
       %6563 = OpShiftLeftLogical %int %6558 %int_1
       %6564 = OpBitwiseXor %int %6561 %6563
       %6569 = OpBitwiseAnd %int %6529 %int_1
       %6573 = OpShiftLeftLogical %int %6569 %int_4
       %6574 = OpShiftLeftLogical %int %6564 %int_6
       %6575 = OpBitwiseOr %int %6573 %6574
       %6576 = OpShiftLeftLogical %int %6558 %int_11
       %6577 = OpBitwiseOr %int %6575 %6576
       %6578 = OpBitwiseAnd %int %6552 %int_15
       %6579 = OpBitwiseOr %int %6577 %6578
       %6580 = OpShiftRightArithmetic %int %6552 %int_4
       %6581 = OpBitwiseAnd %int %6580 %int_1
       %6582 = OpShiftLeftLogical %int %6581 %int_5
       %6583 = OpBitwiseOr %int %6579 %6582
       %6584 = OpShiftRightArithmetic %int %6552 %int_5
       %6585 = OpBitwiseAnd %int %6584 %int_7
       %6586 = OpShiftLeftLogical %int %6585 %int_8
       %6587 = OpBitwiseOr %int %6583 %6586
       %6588 = OpShiftRightArithmetic %int %6552 %int_8
       %6589 = OpShiftLeftLogical %int %6588 %int_12
       %6590 = OpBitwiseOr %int %6587 %6589
       %6424 = OpBitcast %uint %6590
               OpBranch %6431
       %6431 = OpLabel
       %7461 = OpPhi %uint %6424 %6414 %6430 %6425
       %6435 = OpIMul %uint %1797 %2213
       %6436 = OpIMul %uint %7461 %6435
       %6439 = OpIAdd %uint %6436 %6498
       %1704 = OpShiftRightLogical %uint %6439 %int_4
       %6657 = OpIEqual %bool %1805 %uint_1
       %6659 = OpIEqual %bool %1805 %uint_2
       %6660 = OpLogicalOr %bool %6657 %6659
               OpSelectionMerge %6673 None
               OpBranchConditional %6660 %6661 %6673
       %6661 = OpLabel
       %6664 = OpBitwiseAnd %v4uint %7456 %7488
       %6666 = OpShiftLeftLogical %v4uint %6664 %7489
       %6669 = OpBitwiseAnd %v4uint %7456 %7482
       %6671 = OpShiftRightLogical %v4uint %6669 %7489
       %6672 = OpBitwiseOr %v4uint %6666 %6671
               OpBranch %6673
       %6673 = OpLabel
       %7464 = OpPhi %v4uint %7456 %6431 %6672 %6661
       %6677 = OpIEqual %bool %1805 %uint_3
       %6678 = OpLogicalOr %bool %6659 %6677
               OpSelectionMerge %6687 None
               OpBranchConditional %6678 %6679 %6687
       %6679 = OpLabel
       %6682 = OpShiftLeftLogical %v4uint %7464 %7484
       %6685 = OpShiftRightLogical %v4uint %7464 %7484
       %6686 = OpBitwiseOr %v4uint %6682 %6685
               OpBranch %6687
       %6687 = OpLabel
       %7465 = OpPhi %v4uint %7464 %6673 %6686 %6679
       %1709 = OpAccessChain %_ptr_StorageBuffer_v4uint %xe_resolve_dest %int_0 %1704
               OpStore %1709 %7465
       %1712 = OpIAdd %uint %6439 %uint_16
       %1714 = OpShiftRightLogical %uint %1712 %int_4
               OpSelectionMerge %6711 None
               OpBranchConditional %6660 %6699 %6711
       %6699 = OpLabel
       %6702 = OpBitwiseAnd %v4uint %7458 %7488
       %6704 = OpShiftLeftLogical %v4uint %6702 %7489
       %6707 = OpBitwiseAnd %v4uint %7458 %7482
       %6709 = OpShiftRightLogical %v4uint %6707 %7489
       %6710 = OpBitwiseOr %v4uint %6704 %6709
               OpBranch %6711
       %6711 = OpLabel
       %7472 = OpPhi %v4uint %7458 %6687 %6710 %6699
               OpSelectionMerge %6725 None
               OpBranchConditional %6678 %6717 %6725
       %6717 = OpLabel
       %6720 = OpShiftLeftLogical %v4uint %7472 %7484
       %6723 = OpShiftRightLogical %v4uint %7472 %7484
       %6724 = OpBitwiseOr %v4uint %6720 %6723
               OpBranch %6725
       %6725 = OpLabel
       %7473 = OpPhi %v4uint %7472 %6711 %6724 %6717
       %1719 = OpAccessChain %_ptr_StorageBuffer_v4uint %xe_resolve_dest %int_0 %1714
               OpStore %1719 %7473
               OpBranch %1720
       %1720 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t resolve_host_color_32bpp_2xmsaa_scaled_cs[] = {
    0x07230203, 0x00010300, 0x000D000A, 0x00001D4D, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x00000004, 0x6E69616D, 0x00000000, 0x000005E6, 0x00060010, 0x00000004,
    0x00000011, 0x00000008, 0x00000008, 0x00000001, 0x00030003, 0x00000002,
    0x000001CC, 0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F,
    0x5F776F6C, 0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47,
    0x735F5458, 0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572,
    0x74636E75, 0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6E69616D,
    0x00000000, 0x00070005, 0x0000020A, 0x68737570, 0x6E6F635F, 0x625F7473,
    0x6B636F6C, 0x0065785F, 0x00090006, 0x0000020A, 0x00000000, 0x725F6578,
    0x6C6F7365, 0x655F6576, 0x6D617264, 0x666E695F, 0x0000006F, 0x000A0006,
    0x0000020A, 0x00000001, 0x725F6578, 0x6C6F7365, 0x635F6576, 0x64726F6F,
    0x74616E69, 0x6E695F65, 0x00006F66, 0x00090006, 0x0000020A, 0x00000002,
    0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365, 0x6F666E69, 0x00000000,
    0x000B0006, 0x0000020A, 0x00000003, 0x725F6578, 0x6C6F7365, 0x645F6576,
    0x5F747365, 0x726F6F63, 0x616E6964, 0x695F6574, 0x006F666E, 0x00060005,
    0x0000020C, 0x68737570, 0x6E6F635F, 0x5F737473, 0x00006578, 0x000A0005,
    0x000003F9, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x65785F72, 0x6F6C625F, 0x00006B63, 0x000D0006, 0x000003F9, 0x00000000,
    0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x69645F72,
    0x74617073, 0x6F5F6863, 0x65736666, 0x00000074, 0x000B0006, 0x000003F9,
    0x00000001, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x75645F72, 0x625F706D, 0x00657361, 0x000D0006, 0x000003F9, 0x00000002,
    0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x75645F72,
    0x705F706D, 0x68637469, 0x6C69745F, 0x00007365, 0x000D0006, 0x000003F9,
    0x00000003, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x6F735F72, 0x65637275, 0x7361625F, 0x69745F65, 0x0073656C, 0x000E0006,
    0x000003F9, 0x00000004, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F,
    0x6F6C6F63, 0x6F735F72, 0x65637275, 0x7469705F, 0x745F6863, 0x73656C69,
    0x00000000, 0x000D0006, 0x000003F9, 0x00000005, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x68745F72, 0x64616572, 0x756F635F,
    0x785F746E, 0x00000000, 0x000D0006, 0x000003F9, 0x00000006, 0x725F6578,
    0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x68745F72, 0x64616572,
    0x756F635F, 0x795F746E, 0x00000000, 0x000C0006, 0x000003F9, 0x00000007,
    0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x65685F72,
    0x74686769, 0x6163735F, 0x0064656C, 0x000D0006, 0x000003F9, 0x00000008,
    0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x736D5F72,
    0x325F6161, 0x61735F78, 0x656C706D, 0x0000305F, 0x000D0006, 0x000003F9,
    0x00000009, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x736D5F72, 0x325F6161, 0x61735F78, 0x656C706D, 0x0000315F, 0x000A0006,
    0x000003F9, 0x0000000A, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F,
    0x6F6C6F63, 0x6C665F72, 0x00736761, 0x00080005, 0x000003FB, 0x725F6578,
    0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x00000072, 0x000A0005,
    0x0000044A, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x6F735F72, 0x65637275, 0x00000000, 0x00090005, 0x000005CC, 0x725F6578,
    0x6C6F7365, 0x645F6576, 0x5F747365, 0x625F6578, 0x6B636F6C, 0x00000000,
    0x00050006, 0x000005CC, 0x00000000, 0x61746164, 0x00000000, 0x00060005,
    0x000005CE, 0x725F6578, 0x6C6F7365, 0x645F6576, 0x00747365, 0x00080005,
    0x000005E6, 0x475F6C67, 0x61626F6C, 0x766E496C, 0x7461636F, 0x496E6F69,
    0x00000044, 0x00050048, 0x0000020A, 0x00000000, 0x00000023, 0x00000000,
    0x00050048, 0x0000020A, 0x00000001, 0x00000023, 0x00000004, 0x00050048,
    0x0000020A, 0x00000002, 0x00000023, 0x00000008, 0x00050048, 0x0000020A,
    0x00000003, 0x00000023, 0x0000000C, 0x00030047, 0x0000020A, 0x00000002,
    0x00050048, 0x000003F9, 0x00000000, 0x00000023, 0x00000000, 0x00050048,
    0x000003F9, 0x00000001, 0x00000023, 0x00000004, 0x00050048, 0x000003F9,
    0x00000002, 0x00000023, 0x00000008, 0x00050048, 0x000003F9, 0x00000003,
    0x00000023, 0x0000000C, 0x00050048, 0x000003F9, 0x00000004, 0x00000023,
    0x00000010, 0x00050048, 0x000003F9, 0x00000005, 0x00000023, 0x00000014,
    0x00050048, 0x000003F9, 0x00000006, 0x00000023, 0x00000018, 0x00050048,
    0x000003F9, 0x00000007, 0x00000023, 0x0000001C, 0x00050048, 0x000003F9,
    0x00000008, 0x00000023, 0x00000020, 0x00050048, 0x000003F9, 0x00000009,
    0x00000023, 0x00000024, 0x00050048, 0x000003F9, 0x0000000A, 0x00000023,
    0x00000028, 0x00030047, 0x000003F9, 0x00000002, 0x00040047, 0x000003FB,
    0x00000022, 0x00000000, 0x00040047, 0x000003FB, 0x00000021, 0x00000001,
    0x00040047, 0x0000044A, 0x00000022, 0x00000002, 0x00040047, 0x0000044A,
    0x00000021, 0x00000000, 0x00040047, 0x000005CB, 0x00000006, 0x00000010,
    0x00040048, 0x000005CC, 0x00000000, 0x00000019, 0x00050048, 0x000005CC,
    0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x000005CC, 0x00000002,
    0x00040047, 0x000005CE, 0x00000022, 0x00000001, 0x00040047, 0x000005CE,
    0x00000021, 0x00000000, 0x00040047, 0x000005E6, 0x0000000B, 0x0000001C,
    0x00040047, 0x000005EB, 0x0000000B, 0x00000019, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020,
    0x00000000, 0x00040017, 0x00000008, 0x00000006, 0x00000002, 0x00030016,
    0x0000000D, 0x00000020, 0x00040017, 0x0000000F, 0x0000000D, 0x00000004,
    0x00040017, 0x0000001D, 0x00000006, 0x00000004, 0x00040015, 0x0000002B,
    0x00000020, 0x00000001, 0x00040017, 0x00000033, 0x0000002B, 0x00000002,
    0x00040017, 0x0000003A, 0x0000002B, 0x00000003, 0x00020014, 0x0000004D,
    0x0004002B, 0x0000000D, 0x000000CE, 0x00000000, 0x0004002B, 0x0000000D,
    0x000000CF, 0x3F800000, 0x0004002B, 0x00000006, 0x000000DC, 0x00000001,
    0x0004002B, 0x00000006, 0x000000DF, 0x00000002, 0x0004002B, 0x00000006,
    0x000000E5, 0x00FF00FF, 0x0004002B, 0x00000006, 0x000000E8, 0x00000008,
    0x0004002B, 0x00000006, 0x000000EC, 0xFF00FF00, 0x0004002B, 0x00000006,
    0x000000F5, 0x00000003, 0x0004002B, 0x00000006, 0x000000FB, 0x00000010,
    0x0004002B, 0x0000000D, 0x00000109, 0x437F0000, 0x0004002B, 0x0000000D,
    0x0000010B, 0x3F000000, 0x0004002B, 0x00000006, 0x0000010F, 0x00000000,
    0x0004002B, 0x0000002B, 0x00000114, 0x00000008, 0x0004002B, 0x0000002B,
    0x00000119, 0x00000010, 0x0004002B, 0x0000002B, 0x0000011E, 0x00000018,
    0x0004002B, 0x0000000D, 0x00000127, 0x447FC000, 0x0004002B, 0x0000000D,
    0x00000128, 0x40400000, 0x0007002C, 0x0000000F, 0x00000129, 0x00000127,
    0x00000127, 0x00000127, 0x00000128, 0x0004002B, 0x0000002B, 0x00000132,
    0x0000000A, 0x0004002B, 0x0000002B, 0x00000137, 0x00000014, 0x0004002B,
    0x0000002B, 0x0000013C, 0x0000001E, 0x0004002B, 0x0000002B, 0x00000141,
    0x00000004, 0x0004002B, 0x0000002B, 0x00000143, 0x00000006, 0x0004002B,
    0x0000002B, 0x00000146, 0x0000000B, 0x0004002B, 0x0000002B, 0x00000149,
    0x0000000F, 0x0004002B, 0x0000002B, 0x0000014D, 0x00000001, 0x0004002B,
    0x0000002B, 0x0000014F, 0x00000005, 0x0004002B, 0x0000002B, 0x00000153,
    0x00000007, 0x0004002B, 0x0000002B, 0x00000158, 0x0000000C, 0x0004002B,
    0x0000002B, 0x0000016B, 0x00000003, 0x0004002B, 0x0000002B, 0x0000018C,
    0x00000002, 0x0004002B, 0x00000006, 0x000001C4, 0x00000005, 0x0004002B,
    0x00000006, 0x000001C7, 0x00000004, 0x0004002B, 0x0000002B, 0x000001DD,
    0x00000000, 0x0006001E, 0x0000020A, 0x00000006, 0x00000006, 0x00000006,
    0x00000006, 0x00040020, 0x0000020B, 0x00000009, 0x0000020A, 0x0004003B,
    0x0000020B, 0x0000020C, 0x00000009, 0x00040020, 0x0000020D, 0x00000009,
    0x00000006, 0x0004002B, 0x00000006, 0x00000215, 0x000003FF, 0x0004002B,
    0x00000006, 0x00000219, 0x0000000A, 0x0004002B, 0x00000006, 0x00000226,
    0x000007FF, 0x0004002B, 0x00000006, 0x0000022A, 0x00000018, 0x0004002B,
    0x00000006, 0x0000022C, 0x0000000F, 0x0004002B, 0x00000006, 0x00000230,
    0x0000001C, 0x0004002B, 0x00000006, 0x00000237, 0x00000013, 0x0005002C,
    0x00000008, 0x00000238, 0x000000FB, 0x00000237, 0x0004002B, 0x00000006,
    0x0000023A, 0x00000007, 0x0004002B, 0x00000006, 0x0000023F, 0x20000000,
    0x0005002C, 0x00000008, 0x00000250, 0x0000010F, 0x000001C7, 0x0005002C,
    0x00000008, 0x00000254, 0x000001C7, 0x000000DC, 0x0004002B, 0x0000002B,
    0x0000025F, 0x00000009, 0x0004002B, 0x00000006, 0x0000028B, 0x01000000,
    0x0004002B, 0x00000006, 0x0000029C, 0x00000014, 0x0005002C, 0x00000008,
    0x0000029D, 0x0000029C, 0x0000022A, 0x00040017, 0x000002C1, 0x00000006,
    0x00000003, 0x0004002B, 0x00000006, 0x000002F4, 0x000000FF, 0x0004002B,
    0x00000006, 0x00000303, 0xC00FFC00, 0x0004002B, 0x00000006, 0x0000032C,
    0x00000050, 0x0004002B, 0x0000000D, 0x0000034F, 0xBF800000, 0x0004002B,
    0x0000000D, 0x00000355, 0xBF000000, 0x0004002B, 0x0000000D, 0x00000358,
    0x46FFFE00, 0x0004002B, 0x00000006, 0x0000035E, 0x0000FFFF, 0x0004002B,
    0x00000006, 0x00000366, 0x3E800000, 0x0004002B, 0x00000006, 0x0000036C,
    0x00000017, 0x0004002B, 0x00000006, 0x0000036F, 0x0000007D, 0x0004002B,
    0x00000006, 0x00000375, 0x007FFFFF, 0x0004002B, 0x00000006, 0x00000377,
    0x00800000, 0x0004002B, 0x00000006, 0x0000037F, 0xC2000000, 0x0004002B,
    0x00000006, 0x00000387, 0x00007FFF, 0x0004002B, 0x0000000D, 0x00000392,
    0x41FF0000, 0x0004002B, 0x00000006, 0x000003BD, 0x0000001E, 0x00040017,
    0x000003CC, 0x0000000D, 0x00000002, 0x000D001E, 0x000003F9, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00040020, 0x000003FA,
    0x00000002, 0x000003F9, 0x0004003B, 0x000003FA, 0x000003FB, 0x00000002,
    0x00040020, 0x000003FC, 0x00000002, 0x00000006, 0x0005002C, 0x00000008,
    0x00000429, 0x000000DC, 0x0000010F, 0x00090019, 0x00000448, 0x0000000D,
    0x00000001, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000000,
    0x00040020, 0x00000449, 0x00000000, 0x00000448, 0x0004003B, 0x00000449,
    0x0000044A, 0x00000000, 0x0003002A, 0x0000004D, 0x0000046C, 0x00030029,
    0x0000004D, 0x000004DE, 0x0005002C, 0x00000008, 0x000004F0, 0x0000010F,
    0x000000DC, 0x0004002B, 0x00000006, 0x00000594, 0x00000006, 0x0003001D,
    0x000005CB, 0x0000001D, 0x0003001E, 0x000005CC, 0x000005CB, 0x00040020,
    0x000005CD, 0x0000000C, 0x000005CC, 0x0004003B, 0x000005CD, 0x000005CE,
    0x0000000C, 0x00040020, 0x000005D7, 0x0000000C, 0x0000001D, 0x00040020,
    0x000005E5, 0x00000001, 0x000002C1, 0x0004003B, 0x000005E5, 0x000005E6,
    0x00000001, 0x0006002C, 0x000002C1, 0x000005EB, 0x000000E8, 0x000000E8,
    0x000000DC, 0x00030001, 0x00000008, 0x00001C0E, 0x0005002C, 0x00000008,
    0x00001D32, 0x0000023A, 0x0000023A, 0x0005002C, 0x00000008, 0x00001D33,
    0x000000DC, 0x000000DC, 0x0005002C, 0x00000008, 0x00001D34, 0x0000010F,
    0x0000010F, 0x0005002C, 0x00000008, 0x00001D35, 0x000000F5, 0x000000F5,
    0x0005002C, 0x00000008, 0x00001D36, 0x0000022C, 0x0000022C, 0x0007002C,
    0x0000000F, 0x00001D37, 0x000000CE, 0x000000CE, 0x000000CE, 0x000000CE,
    0x0007002C, 0x0000000F, 0x00001D38, 0x000000CF, 0x000000CF, 0x000000CF,
    0x000000CF, 0x0007002C, 0x0000000F, 0x00001D39, 0x0000010B, 0x0000010B,
    0x0000010B, 0x0000010B, 0x0007002C, 0x0000001D, 0x00001D3A, 0x000000EC,
    0x000000EC, 0x000000EC, 0x000000EC, 0x0007002C, 0x0000001D, 0x00001D3B,
    0x000002F4, 0x000002F4, 0x000002F4, 0x000002F4, 0x0007002C, 0x0000001D,
    0x00001D3C, 0x000000FB, 0x000000FB, 0x000000FB, 0x000000FB, 0x0007002C,
    0x0000001D, 0x00001D3D, 0x00000303, 0x00000303, 0x00000303, 0x00000303,
    0x0007002C, 0x0000001D, 0x00001D3E, 0x00000215, 0x00000215, 0x00000215,
    0x00000215, 0x0007002C, 0x0000001D, 0x00001D3F, 0x0000029C, 0x0000029C,
    0x0000029C, 0x0000029C, 0x0007002C, 0x0000001D, 0x00001D40, 0x000000E5,
    0x000000E5, 0x000000E5, 0x000000E5, 0x0007002C, 0x0000001D, 0x00001D41,
    0x000000E8, 0x000000E8, 0x000000E8, 0x000000E8, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0004003D,
    0x000002C1, 0x000005E8, 0x000005E6, 0x000300F7, 0x000006B8, 0x00000000,
    0x000300FB, 0x0000010F, 0x00000648, 0x000200F8, 0x00000648, 0x00050041,
    0x0000020D, 0x000006C5, 0x0000020C, 0x000001DD, 0x0004003D, 0x00000006,
    0x000006C6, 0x000006C5, 0x00050041, 0x0000020D, 0x000006C7, 0x0000020C,
    0x0000014D, 0x0004003D, 0x00000006, 0x000006C8, 0x000006C7, 0x000500C2,
    0x00000006, 0x000006D9, 0x000006C6, 0x0000022A, 0x000500C7, 0x00000006,
    0x000006DA, 0x000006D9, 0x0000022C, 0x00050050, 0x00000008, 0x00000745,
    0x000006C8, 0x000006C8, 0x000500C2, 0x00000008, 0x000006E2, 0x00000745,
    0x00000238, 0x000500C7, 0x00000008, 0x000006E4, 0x000006E2, 0x00001D32,
    0x000500C7, 0x00000006, 0x000006E7, 0x000006C6, 0x0000023F, 0x000500AB,
    0x0000004D, 0x000006E8, 0x000006E7, 0x0000010F, 0x000300F7, 0x000006F2,
    0x00000000, 0x000400FA, 0x000006E8, 0x000006E9, 0x000006EF, 0x000200F8,
    0x000006EF, 0x000200F9, 0x000006F2, 0x000200F8, 0x000006E9, 0x000500C2,
    0x00000008, 0x000006ED, 0x000006E4, 0x00001D33, 0x000200F9, 0x000006F2,
    0x000200F8, 0x000006F2, 0x000700F5, 0x00000008, 0x00001C09, 0x000006ED,
    0x000006E9, 0x00001D34, 0x000006EF, 0x000500C2, 0x00000008, 0x000006F5,
    0x00000745, 0x00000250, 0x000500C4, 0x00000008, 0x000006F7, 0x00001D33,
    0x00000254, 0x00050082, 0x00000008, 0x000006F9, 0x000006F7, 0x00001D33,
    0x000500C7, 0x00000008, 0x000006FA, 0x000006F5, 0x000006F9, 0x000500C4,
    0x00000008, 0x000006FC, 0x000006FA, 0x00001D35, 0x00050084, 0x00000008,
    0x000006FF, 0x000006FC, 0x000006E4, 0x000500C2, 0x00000006, 0x00000702,
    0x000006C8, 0x000001C4, 0x000500C7, 0x00000006, 0x00000703, 0x00000702,
    0x00000226, 0x00050051, 0x00000006, 0x00000705, 0x000006E4, 0x00000000,
    0x00050084, 0x00000006, 0x00000706, 0x00000703, 0x00000705, 0x00050041,
    0x0000020D, 0x00000708, 0x0000020C, 0x0000018C, 0x0004003D, 0x00000006,
    0x00000709, 0x00000708, 0x00050041, 0x0000020D, 0x0000070A, 0x0000020C,
    0x0000016B, 0x0004003D, 0x00000006, 0x0000070B, 0x0000070A, 0x000500C7,
    0x00000006, 0x0000070D, 0x00000709, 0x0000023A, 0x000500C7, 0x00000006,
    0x00000710, 0x00000709, 0x000000E8, 0x000500AB, 0x0000004D, 0x00000711,
    0x00000710, 0x0000010F, 0x000500C2, 0x00000006, 0x00000714, 0x00000709,
    0x000001C7, 0x000500C7, 0x00000006, 0x00000715, 0x00000714, 0x0000023A,
    0x000500C7, 0x00000006, 0x00000725, 0x00000709, 0x0000028B, 0x000500AB,
    0x0000004D, 0x00000726, 0x00000725, 0x0000010F, 0x000500C7, 0x00000006,
    0x00000729, 0x0000070B, 0x00000215, 0x000500C2, 0x00000006, 0x0000072C,
    0x0000070B, 0x00000219, 0x000500C7, 0x00000006, 0x0000072D, 0x0000072C,
    0x00000215, 0x000500C4, 0x00000006, 0x0000072E, 0x0000072D, 0x0000014D,
    0x00050050, 0x00000008, 0x00000759, 0x0000070B, 0x0000070B, 0x000500C2,
    0x00000008, 0x00000732, 0x00000759, 0x0000029D, 0x000500C7, 0x00000008,
    0x00000734, 0x00000732, 0x00001D36, 0x000500C4, 0x00000008, 0x00000736,
    0x00000734, 0x00001D35, 0x00050084, 0x00000008, 0x00000739, 0x00000736,
    0x000006E4, 0x000500C2, 0x00000006, 0x0000073C, 0x0000070B, 0x00000230,
    0x000500C7, 0x00000006, 0x0000073D, 0x0000073C, 0x0000023A, 0x000300F7,
    0x000007DD, 0x00000000, 0x000300FB, 0x0000010F, 0x0000076E, 0x000200F8,
    0x0000076E, 0x00050051, 0x00000006, 0x00000770, 0x000005E8, 0x00000000,
    0x00050041, 0x000003FC, 0x00000771, 0x000003FB, 0x0000014F, 0x0004003D,
    0x00000006, 0x00000772, 0x00000771, 0x000500AE, 0x0000004D, 0x00000773,
    0x00000770, 0x00000772, 0x000400A8, 0x0000004D, 0x00000774, 0x00000773,
    0x000300F7, 0x0000077B, 0x00000000, 0x000400FA, 0x00000774, 0x00000775,
    0x0000077B, 0x000200F8, 0x00000775, 0x00050051, 0x00000006, 0x00000777,
    0x000005E8, 0x00000001, 0x00050041, 0x000003FC, 0x00000778, 0x000003FB,
    0x00000143, 0x0004003D, 0x00000006, 0x00000779, 0x00000778, 0x000500AE,
    0x0000004D, 0x0000077A, 0x00000777, 0x00000779, 0x000200F9, 0x0000077B,
    0x000200F8, 0x0000077B, 0x000700F5, 0x0000004D, 0x0000077C, 0x00000773,
    0x0000076E, 0x0000077A, 0x00000775, 0x000300F7, 0x0000077E, 0x00000000,
    0x000400FA, 0x0000077C, 0x0000077D, 0x0000077E, 0x000200F8, 0x0000077D,
    0x000200F9, 0x000007DD, 0x000200F8, 0x0000077E, 0x00050084, 0x00000006,
    0x000007EA, 0x0000032C, 0x00000705, 0x00050051, 0x00000006, 0x000007F4,
    0x000006E4, 0x00000001, 0x00050084, 0x00000006, 0x000007F5, 0x000000FB,
    0x000007F4, 0x000500C2, 0x00000006, 0x000007F0, 0x000007F5, 0x000000DC,
    0x00050084, 0x00000006, 0x00000787, 0x00000770, 0x000000E8, 0x00050051,
    0x00000006, 0x00000789, 0x000005E8, 0x00000001, 0x00050086, 0x00000006,
    0x0000078C, 0x00000787, 0x000007EA, 0x00050086, 0x00000006, 0x0000078F,
    0x00000789, 0x000007F0, 0x00050084, 0x00000006, 0x00000793, 0x0000078C,
    0x000007EA, 0x00050082, 0x00000006, 0x00000794, 0x00000787, 0x00000793,
    0x00050084, 0x00000006, 0x00000798, 0x0000078F, 0x000007F0, 0x00050082,
    0x00000006, 0x00000799, 0x00000789, 0x00000798, 0x00050041, 0x000003FC,
    0x0000079A, 0x000003FB, 0x000001DD, 0x0004003D, 0x00000006, 0x0000079B,
    0x0000079A, 0x00050041, 0x000003FC, 0x0000079D, 0x000003FB, 0x0000018C,
    0x0004003D, 0x00000006, 0x0000079E, 0x0000079D, 0x00050084, 0x00000006,
    0x0000079F, 0x0000078F, 0x0000079E, 0x00050080, 0x00000006, 0x000007A0,
    0x0000079B, 0x0000079F, 0x00050080, 0x00000006, 0x000007A2, 0x000007A0,
    0x0000078C, 0x00050086, 0x00000006, 0x000007A7, 0x000007A2, 0x0000079E,
    0x00050084, 0x00000006, 0x000007AB, 0x000007A7, 0x0000079E, 0x00050082,
    0x00000006, 0x000007AC, 0x000007A2, 0x000007AB, 0x00050084, 0x00000006,
    0x000007AF, 0x000007AC, 0x000007EA, 0x00050080, 0x00000006, 0x000007B1,
    0x000007AF, 0x00000794, 0x00050084, 0x00000006, 0x000007B4, 0x000007A7,
    0x000007F0, 0x00050080, 0x00000006, 0x000007B6, 0x000007B4, 0x00000799,
    0x00050050, 0x00000008, 0x000007B7, 0x000007B1, 0x000007B6, 0x00050051,
    0x00000006, 0x000007BB, 0x000006FF, 0x00000000, 0x000500B0, 0x0000004D,
    0x000007BC, 0x000007B1, 0x000007BB, 0x000400A8, 0x0000004D, 0x000007BD,
    0x000007BC, 0x000300F7, 0x000007C4, 0x00000000, 0x000400FA, 0x000007BD,
    0x000007BE, 0x000007C4, 0x000200F8, 0x000007BE, 0x00050051, 0x00000006,
    0x000007C2, 0x000006FF, 0x00000001, 0x000500B0, 0x0000004D, 0x000007C3,
    0x000007B6, 0x000007C2, 0x000200F9, 0x000007C4, 0x000200F8, 0x000007C4,
    0x000700F5, 0x0000004D, 0x000007C5, 0x000007BC, 0x0000077E, 0x000007C3,
    0x000007BE, 0x000300F7, 0x000007C7, 0x00000000, 0x000400FA, 0x000007C5,
    0x000007C6, 0x000007C7, 0x000200F8, 0x000007C6, 0x000200F9, 0x000007DD,
    0x000200F8, 0x000007C7, 0x00050082, 0x00000008, 0x000007CB, 0x000007B7,
    0x000006FF, 0x00050051, 0x00000006, 0x000007CD, 0x000007CB, 0x00000000,
    0x000500C4, 0x00000006, 0x000007D0, 0x00000706, 0x000000F5, 0x000500AE,
    0x0000004D, 0x000007D1, 0x000007CD, 0x000007D0, 0x000400A8, 0x0000004D,
    0x000007D2, 0x000007D1, 0x000300F7, 0x000007D9, 0x00000000, 0x000400FA,
    0x000007D2, 0x000007D3, 0x000007D9, 0x000200F8, 0x000007D3, 0x00050051,
    0x00000006, 0x000007D5, 0x000007CB, 0x00000001, 0x00050041, 0x000003FC,
    0x000007D6, 0x000003FB, 0x00000153, 0x0004003D, 0x00000006, 0x000007D7,
    0x000007D6, 0x000500AE, 0x0000004D, 0x000007D8, 0x000007D5, 0x000007D7,
    0x000200F9, 0x000007D9, 0x000200F8, 0x000007D9, 0x000700F5, 0x0000004D,
    0x000007DA, 0x000007D1, 0x000007C7, 0x000007D8, 0x000007D3, 0x000300F7,
    0x000007DC, 0x00000000, 0x000400FA, 0x000007DA, 0x000007DB, 0x000007DC,
    0x000200F8, 0x000007DB, 0x000200F9, 0x000007DD, 0x000200F8, 0x000007DC,
    0x000200F9, 0x000007DD, 0x000200F8, 0x000007DD, 0x000B00F5, 0x00000008,
    0x00001C0B, 0x00001C0E, 0x0000077D, 0x00001C0E, 0x000007C6, 0x000007CB,
    0x000007DB, 0x000007CB, 0x000007DC, 0x000B00F5, 0x0000004D, 0x00001C0A,
    0x0000046C, 0x0000077D, 0x0000046C, 0x000007C6, 0x0000046C, 0x000007DB,
    0x000004DE, 0x000007DC, 0x000400A8, 0x0000004D, 0x0000064E, 0x00001C0A,
    0x000300F7, 0x00000650, 0x00000000, 0x000400FA, 0x0000064E, 0x0000064F,
    0x00000650, 0x000200F8, 0x0000064F, 0x000200F9, 0x000006B8, 0x000200F8,
    0x00000650, 0x000500B2, 0x0000004D, 0x00000808, 0x0000073D, 0x000000F5,
    0x000300F7, 0x00000811, 0x00000000, 0x000400FA, 0x00000808, 0x00000809,
    0x0000080B, 0x000200F8, 0x0000080B, 0x000500AA, 0x0000004D, 0x0000080D,
    0x0000073D, 0x000001C4, 0x000600A9, 0x00000006, 0x00001D45, 0x0000080D,
    0x000000DF, 0x0000010F, 0x000200F9, 0x00000811, 0x000200F8, 0x00000809,
    0x000200F9, 0x00000811, 0x000200F8, 0x00000811, 0x000700F5, 0x00000006,
    0x00001C11, 0x0000073D, 0x00000809, 0x00001D45, 0x0000080B, 0x00050051,
    0x00000006, 0x00000818, 0x00001C0B, 0x00000000, 0x00050051, 0x00000006,
    0x0000081C, 0x00001C0B, 0x00000001, 0x00050051, 0x00000006, 0x0000081E,
    0x00001C09, 0x00000001, 0x0007000C, 0x00000006, 0x0000081F, 0x00000001,
    0x00000029, 0x0000081C, 0x0000081E, 0x00050050, 0x00000008, 0x00000820,
    0x00000818, 0x0000081F, 0x00050080, 0x00000008, 0x00000823, 0x00000820,
    0x000006FF, 0x000500C4, 0x00000008, 0x00000825, 0x00000823, 0x000004F0,
    0x00050050, 0x00000008, 0x00000835, 0x00001C11, 0x00001C11, 0x000500C2,
    0x00000008, 0x0000082E, 0x00000835, 0x00000429, 0x000500C7, 0x00000008,
    0x00000830, 0x0000082E, 0x00001D33, 0x00050080, 0x00000008, 0x00000828,
    0x00000825, 0x00000830, 0x00050084, 0x00000006, 0x000008A1, 0x0000032C,
    0x00000705, 0x00050051, 0x00000006, 0x000008A5, 0x000006E4, 0x00000001,
    0x00050084, 0x00000006, 0x000008A6, 0x000000FB, 0x000008A5, 0x00050051,
    0x00000006, 0x00000867, 0x00000828, 0x00000000, 0x00050086, 0x00000006,
    0x00000869, 0x00000867, 0x000008A1, 0x00050051, 0x00000006, 0x0000086B,
    0x00000828, 0x00000001, 0x00050086, 0x00000006, 0x0000086D, 0x0000086B,
    0x000008A6, 0x00050084, 0x00000006, 0x00000872, 0x00000869, 0x000008A1,
    0x00050082, 0x00000006, 0x00000873, 0x00000867, 0x00000872, 0x00050084,
    0x00000006, 0x00000878, 0x0000086D, 0x000008A6, 0x00050082, 0x00000006,
    0x00000879, 0x0000086B, 0x00000878, 0x00050041, 0x000003FC, 0x0000087B,
    0x000003FB, 0x0000018C, 0x0004003D, 0x00000006, 0x0000087C, 0x0000087B,
    0x00050084, 0x00000006, 0x0000087D, 0x0000086D, 0x0000087C, 0x00050080,
    0x00000006, 0x0000087F, 0x0000087D, 0x00000869, 0x00050041, 0x000003FC,
    0x00000880, 0x000003FB, 0x0000014D, 0x0004003D, 0x00000006, 0x00000881,
    0x00000880, 0x00050080, 0x00000006, 0x00000883, 0x00000881, 0x0000087F,
    0x00050041, 0x000003FC, 0x00000885, 0x000003FB, 0x0000016B, 0x0004003D,
    0x00000006, 0x00000886, 0x00000885, 0x00050082, 0x00000006, 0x00000887,
    0x00000883, 0x00000886, 0x00050041, 0x000003FC, 0x00000888, 0x000003FB,
    0x00000141, 0x0004003D, 0x00000006, 0x00000889, 0x00000888, 0x00050086,
    0x00000006, 0x0000088C, 0x00000887, 0x00000889, 0x00050084, 0x00000006,
    0x00000890, 0x0000088C, 0x00000889, 0x00050082, 0x00000006, 0x00000891,
    0x00000887, 0x00000890, 0x00050084, 0x00000006, 0x00000894, 0x00000891,
    0x000008A1, 0x00050080, 0x00000006, 0x00000896, 0x00000894, 0x00000873,
    0x00050084, 0x00000006, 0x00000899, 0x0000088C, 0x000008A6, 0x00050080,
    0x00000006, 0x0000089B, 0x00000899, 0x00000879, 0x000500C7, 0x00000006,
    0x000008AB, 0x0000089B, 0x000000DC, 0x000500AB, 0x0000004D, 0x000008AC,
    0x000008AB, 0x0000010F, 0x000300F7, 0x000008B3, 0x00000000, 0x000400FA,
    0x000008AC, 0x000008AD, 0x000008B0, 0x000200F8, 0x000008B0, 0x00050041,
    0x000003FC, 0x000008B1, 0x000003FB, 0x00000114, 0x0004003D, 0x00000006,
    0x000008B2, 0x000008B1, 0x000200F9, 0x000008B3, 0x000200F8, 0x000008AD,
    0x00050041, 0x000003FC, 0x000008AE, 0x000003FB, 0x0000025F, 0x0004003D,
    0x00000006, 0x000008AF, 0x000008AE, 0x000200F9, 0x000008B3, 0x000200F8,
    0x000008B3, 0x000700F5, 0x00000006, 0x00001C13, 0x000008AF, 0x000008AD,
    0x000008B2, 0x000008B0, 0x0004003D, 0x00000448, 0x00000843, 0x0000044A,
    0x0004007C, 0x0000002B, 0x00000846, 0x00000896, 0x000500C2, 0x00000006,
    0x00000849, 0x0000089B, 0x000000DC, 0x0004007C, 0x0000002B, 0x0000084A,
    0x00000849, 0x00050050, 0x00000033, 0x0000084E, 0x00000846, 0x0000084A,
    0x0004007C, 0x0000002B, 0x00000850, 0x00001C13, 0x0007005F, 0x0000000F,
    0x00000851, 0x00000843, 0x0000084E, 0x00000040, 0x00000850, 0x000300F7,
    0x000008EE, 0x00000000, 0x001300FB, 0x000006DA, 0x000008C4, 0x00000000,
    0x000008C8, 0x00000001, 0x000008C8, 0x00000002, 0x000008CB, 0x0000000A,
    0x000008CB, 0x00000003, 0x000008CE, 0x0000000C, 0x000008CE, 0x00000004,
    0x000008E1, 0x00000006, 0x000008EA, 0x000200F8, 0x000008EA, 0x0007004F,
    0x000003CC, 0x000008EC, 0x00000851, 0x00000851, 0x00000000, 0x00000001,
    0x0006000C, 0x00000006, 0x000008ED, 0x00000001, 0x0000003A, 0x000008EC,
    0x000200F9, 0x000008EE, 0x000200F8, 0x000008E1, 0x00050051, 0x0000000D,
    0x000008E3, 0x00000851, 0x00000000, 0x0007000C, 0x0000000D, 0x000009EB,
    0x00000001, 0x00000028, 0x000008E3, 0x0000034F, 0x0007000C, 0x0000000D,
    0x000009EC, 0x00000001, 0x00000025, 0x000009EB, 0x000000CF, 0x000500BE,
    0x0000004D, 0x000009EE, 0x000009EC, 0x000000CE, 0x000600A9, 0x0000000D,
    0x000009EF, 0x000009EE, 0x0000010B, 0x00000355, 0x0008000C, 0x0000000D,
    0x000009F3, 0x00000001, 0x00000032, 0x000009EC, 0x00000358, 0x000009EF,
    0x0004006E, 0x0000002B, 0x000009F4, 0x000009F3, 0x0004007C, 0x00000006,
    0x000009F5, 0x000009F4, 0x000500C7, 0x00000006, 0x000009F6, 0x000009F5,
    0x0000035E, 0x00050051, 0x0000000D, 0x000008E6, 0x00000851, 0x00000001,
    0x0007000C, 0x0000000D, 0x000009FC, 0x00000001, 0x00000028, 0x000008E6,
    0x0000034F, 0x0007000C, 0x0000000D, 0x000009FD, 0x00000001, 0x00000025,
    0x000009FC, 0x000000CF, 0x000500BE, 0x0000004D, 0x000009FF, 0x000009FD,
    0x000000CE, 0x000600A9, 0x0000000D, 0x00000A00, 0x000009FF, 0x0000010B,
    0x00000355, 0x0008000C, 0x0000000D, 0x00000A04, 0x00000001, 0x00000032,
    0x000009FD, 0x00000358, 0x00000A00, 0x0004006E, 0x0000002B, 0x00000A05,
    0x00000A04, 0x0004007C, 0x00000006, 0x00000A06, 0x00000A05, 0x000500C7,
    0x00000006, 0x00000A07, 0x00000A06, 0x0000035E, 0x000500C4, 0x00000006,
    0x000008E8, 0x00000A07, 0x000000FB, 0x000500C5, 0x00000006, 0x000008E9,
    0x000009F6, 0x000008E8, 0x000200F9, 0x000008EE, 0x000200F8, 0x000008CE,
    0x00050051, 0x0000000D, 0x000008D0, 0x00000851, 0x00000000, 0x0007000C,
    0x0000000D, 0x00000953, 0x00000001, 0x00000028, 0x000008D0, 0x000000CE,
    0x0007000C, 0x0000000D, 0x00000954, 0x00000001, 0x00000025, 0x00000953,
    0x00000392, 0x0004007C, 0x00000006, 0x00000960, 0x00000954, 0x000500B0,
    0x0000004D, 0x00000962, 0x00000960, 0x00000366, 0x000300F7, 0x00000972,
    0x00000000, 0x000400FA, 0x00000962, 0x00000963, 0x0000096F, 0x000200F8,
    0x0000096F, 0x00050080, 0x00000006, 0x00000971, 0x00000960, 0x0000037F,
    0x000200F9, 0x00000972, 0x000200F8, 0x00000963, 0x000500C2, 0x00000006,
    0x00000965, 0x00000960, 0x0000036C, 0x00050082, 0x00000006, 0x00000967,
    0x0000036F, 0x00000965, 0x0007000C, 0x00000006, 0x00000968, 0x00000001,
    0x00000026, 0x00000967, 0x0000022A, 0x000500C7, 0x00000006, 0x0000096A,
    0x00000960, 0x00000375, 0x000500C5, 0x00000006, 0x0000096B, 0x0000096A,
    0x00000377, 0x000500C2, 0x00000006, 0x0000096E, 0x0000096B, 0x00000968,
    0x000200F9, 0x00000972, 0x000200F8, 0x00000972, 0x000700F5, 0x00000006,
    0x00001C14, 0x0000096E, 0x00000963, 0x00000971, 0x0000096F, 0x000500C2,
    0x00000006, 0x00000974, 0x00001C14, 0x000000FB, 0x000500C7, 0x00000006,
    0x00000975, 0x00000974, 0x000000DC, 0x00050080, 0x00000006, 0x00000977,
    0x00001C14, 0x00000387, 0x00050080, 0x00000006, 0x00000979, 0x00000977,
    0x00000975, 0x000500C2, 0x00000006, 0x0000097B, 0x00000979, 0x000000FB,
    0x000500C7, 0x00000006, 0x0000097C, 0x0000097B, 0x00000215, 0x00050051,
    0x0000000D, 0x000008D3, 0x00000851, 0x00000001, 0x0007000C, 0x0000000D,
    0x00000981, 0x00000001, 0x00000028, 0x000008D3, 0x000000CE, 0x0007000C,
    0x0000000D, 0x00000982, 0x00000001, 0x00000025, 0x00000981, 0x00000392,
    0x0004007C, 0x00000006, 0x0000098E, 0x00000982, 0x000500B0, 0x0000004D,
    0x00000990, 0x0000098E, 0x00000366, 0x000300F7, 0x000009A0, 0x00000000,
    0x000400FA, 0x00000990, 0x00000991, 0x0000099D, 0x000200F8, 0x0000099D,
    0x00050080, 0x00000006, 0x0000099F, 0x0000098E, 0x0000037F, 0x000200F9,
    0x000009A0, 0x000200F8, 0x00000991, 0x000500C2, 0x00000006, 0x00000993,
    0x0000098E, 0x0000036C, 0x00050082, 0x00000006, 0x00000995, 0x0000036F,
    0x00000993, 0x0007000C, 0x00000006, 0x00000996, 0x00000001, 0x00000026,
    0x00000995, 0x0000022A, 0x000500C7, 0x00000006, 0x00000998, 0x0000098E,
    0x00000375, 0x000500C5, 0x00000006, 0x00000999, 0x00000998, 0x00000377,
    0x000500C2, 0x00000006, 0x0000099C, 0x00000999, 0x00000996, 0x000200F9,
    0x000009A0, 0x000200F8, 0x000009A0, 0x000700F5, 0x00000006, 0x00001C15,
    0x0000099C, 0x00000991, 0x0000099F, 0x0000099D, 0x000500C2, 0x00000006,
    0x000009A2, 0x00001C15, 0x000000FB, 0x000500C7, 0x00000006, 0x000009A3,
    0x000009A2, 0x000000DC, 0x00050080, 0x00000006, 0x000009A5, 0x00001C15,
    0x00000387, 0x00050080, 0x00000006, 0x000009A7, 0x000009A5, 0x000009A3,
    0x000500C2, 0x00000006, 0x000009A9, 0x000009A7, 0x000000FB, 0x000500C7,
    0x00000006, 0x000009AA, 0x000009A9, 0x00000215, 0x000500C4, 0x00000006,
    0x000008D5, 0x000009AA, 0x00000219, 0x000500C5, 0x00000006, 0x000008D6,
    0x0000097C, 0x000008D5, 0x00050051, 0x0000000D, 0x000008D8, 0x00000851,
    0x00000002, 0x0007000C, 0x0000000D, 0x000009AF, 0x00000001, 0x00000028,
    0x000008D8, 0x000000CE, 0x0007000C, 0x0000000D, 0x000009B0, 0x00000001,
    0x00000025, 0x000009AF, 0x00000392, 0x0004007C, 0x00000006, 0x000009BC,
    0x000009B0, 0x000500B0, 0x0000004D, 0x000009BE, 0x000009BC, 0x00000366,
    0x000300F7, 0x000009CE, 0x00000000, 0x000400FA, 0x000009BE, 0x000009BF,
    0x000009CB, 0x000200F8, 0x000009CB, 0x00050080, 0x00000006, 0x000009CD,
    0x000009BC, 0x0000037F, 0x000200F9, 0x000009CE, 0x000200F8, 0x000009BF,
    0x000500C2, 0x00000006, 0x000009C1, 0x000009BC, 0x0000036C, 0x00050082,
    0x00000006, 0x000009C3, 0x0000036F, 0x000009C1, 0x0007000C, 0x00000006,
    0x000009C4, 0x00000001, 0x00000026, 0x000009C3, 0x0000022A, 0x000500C7,
    0x00000006, 0x000009C6, 0x000009BC, 0x00000375, 0x000500C5, 0x00000006,
    0x000009C7, 0x000009C6, 0x00000377, 0x000500C2, 0x00000006, 0x000009CA,
    0x000009C7, 0x000009C4, 0x000200F9, 0x000009CE, 0x000200F8, 0x000009CE,
    0x000700F5, 0x00000006, 0x00001C16, 0x000009CA, 0x000009BF, 0x000009CD,
    0x000009CB, 0x000500C2, 0x00000006, 0x000009D0, 0x00001C16, 0x000000FB,
    0x000500C7, 0x00000006, 0x000009D1, 0x000009D0, 0x000000DC, 0x00050080,
    0x00000006, 0x000009D3, 0x00001C16, 0x00000387, 0x00050080, 0x00000006,
    0x000009D5, 0x000009D3, 0x000009D1, 0x000500C2, 0x00000006, 0x000009D7,
    0x000009D5, 0x000000FB, 0x000500C7, 0x00000006, 0x000009D8, 0x000009D7,
    0x00000215, 0x000500C4, 0x00000006, 0x000008DA, 0x000009D8, 0x0000029C,
    0x000500C5, 0x00000006, 0x000008DB, 0x000008D6, 0x000008DA, 0x00050051,
    0x0000000D, 0x000008DD, 0x00000851, 0x00000003, 0x0008000C, 0x0000000D,
    0x000009E5, 0x00000001, 0x0000002B, 0x000008DD, 0x000000CE, 0x000000CF,
    0x0008000C, 0x0000000D, 0x000009E0, 0x00000001, 0x00000032, 0x000009E5,
    0x00000128, 0x0000010B, 0x0004006D, 0x00000006, 0x000009E1, 0x000009E0,
    0x000500C4, 0x00000006, 0x000008DF, 0x000009E1, 0x000003BD, 0x000500C5,
    0x00000006, 0x000008E0, 0x000008DB, 0x000008DF, 0x000200F9, 0x000008EE,
    0x000200F8, 0x000008CB, 0x0008000C, 0x0000000F, 0x00000940, 0x00000001,
    0x0000002B, 0x00000851, 0x00001D37, 0x00001D38, 0x0008000C, 0x0000000F,
    0x00000929, 0x00000001, 0x00000032, 0x00000940, 0x00000129, 0x00001D39,
    0x0004006D, 0x0000001D, 0x0000092A, 0x00000929, 0x00050051, 0x00000006,
    0x0000092C, 0x0000092A, 0x00000000, 0x00050051, 0x00000006, 0x0000092E,
    0x0000092A, 0x00000001, 0x000500C4, 0x00000006, 0x0000092F, 0x0000092E,
    0x00000132, 0x000500C5, 0x00000006, 0x00000930, 0x0000092C, 0x0000092F,
    0x00050051, 0x00000006, 0x00000932, 0x0000092A, 0x00000002, 0x000500C4,
    0x00000006, 0x00000933, 0x00000932, 0x00000137, 0x000500C5, 0x00000006,
    0x00000934, 0x00000930, 0x00000933, 0x00050051, 0x00000006, 0x00000936,
    0x0000092A, 0x00000003, 0x000500C4, 0x00000006, 0x00000937, 0x00000936,
    0x0000013C, 0x000500C5, 0x00000006, 0x00000938, 0x00000934, 0x00000937,
    0x000200F9, 0x000008EE, 0x000200F8, 0x000008C8, 0x0008000C, 0x0000000F,
    0x00000912, 0x00000001, 0x0000002B, 0x00000851, 0x00001D37, 0x00001D38,
    0x0005008E, 0x0000000F, 0x000008F9, 0x00000912, 0x00000109, 0x00050081,
    0x0000000F, 0x000008FB, 0x000008F9, 0x00001D39, 0x0004006D, 0x0000001D,
    0x000008FC, 0x000008FB, 0x00050051, 0x00000006, 0x000008FE, 0x000008FC,
    0x00000000, 0x00050051, 0x00000006, 0x00000900, 0x000008FC, 0x00000001,
    0x000500C4, 0x00000006, 0x00000901, 0x00000900, 0x00000114, 0x000500C5,
    0x00000006, 0x00000902, 0x000008FE, 0x00000901, 0x00050051, 0x00000006,
    0x00000904, 0x000008FC, 0x00000002, 0x000500C4, 0x00000006, 0x00000905,
    0x00000904, 0x00000119, 0x000500C5, 0x00000006, 0x00000906, 0x00000902,
    0x00000905, 0x00050051, 0x00000006, 0x00000908, 0x000008FC, 0x00000003,
    0x000500C4, 0x00000006, 0x00000909, 0x00000908, 0x0000011E, 0x000500C5,
    0x00000006, 0x0000090A, 0x00000906, 0x00000909, 0x000200F9, 0x000008EE,
    0x000200F8, 0x000008C4, 0x00050051, 0x0000000D, 0x000008C6, 0x00000851,
    0x00000000, 0x0004007C, 0x00000006, 0x000008C7, 0x000008C6, 0x000200F9,
    0x000008EE, 0x000200F8, 0x000008EE, 0x000F00F5, 0x00000006, 0x00001C19,
    0x000008C7, 0x000008C4, 0x0000090A, 0x000008C8, 0x00000938, 0x000008CB,
    0x000008E0, 0x000009CE, 0x000008E9, 0x000008E1, 0x000008ED, 0x000008EA,
    0x000300F7, 0x00000A23, 0x00000000, 0x000400FA, 0x00000808, 0x00000A1B,
    0x00000A1D, 0x000200F8, 0x00000A1D, 0x000500AA, 0x0000004D, 0x00000A1F,
    0x0000073D, 0x000001C4, 0x000600A9, 0x00000006, 0x00001D46, 0x00000A1F,
    0x000000DF, 0x0000010F, 0x000200F9, 0x00000A23, 0x000200F8, 0x00000A1B,
    0x000200F9, 0x00000A23, 0x000200F8, 0x00000A23, 0x000700F5, 0x00000006,
    0x00001C2B, 0x0000073D, 0x00000A1B, 0x00001D46, 0x00000A1D, 0x00050080,
    0x00000006, 0x00000A2C, 0x00000818, 0x000000DC, 0x00050050, 0x00000008,
    0x00000A32, 0x00000A2C, 0x0000081F, 0x00050080, 0x00000008, 0x00000A35,
    0x00000A32, 0x000006FF, 0x000500C4, 0x00000008, 0x00000A37, 0x00000A35,
    0x000004F0, 0x00050050, 0x00000008, 0x00000A47, 0x00001C2B, 0x00001C2B,
    0x000500C2, 0x00000008, 0x00000A40, 0x00000A47, 0x00000429, 0x000500C7,
    0x00000008, 0x00000A42, 0x00000A40, 0x00001D33, 0x00050080, 0x00000008,
    0x00000A3A, 0x00000A37, 0x00000A42, 0x00050051, 0x00000006, 0x00000A79,
    0x00000A3A, 0x00000000, 0x00050086, 0x00000006, 0x00000A7B, 0x00000A79,
    0x000008A1, 0x00050051, 0x00000006, 0x00000A7D, 0x00000A3A, 0x00000001,
    0x00050086, 0x00000006, 0x00000A7F, 0x00000A7D, 0x000008A6, 0x00050084,
    0x00000006, 0x00000A84, 0x00000A7B, 0x000008A1, 0x00050082, 0x00000006,
    0x00000A85, 0x00000A79, 0x00000A84, 0x00050084, 0x00000006, 0x00000A8A,
    0x00000A7F, 0x000008A6, 0x00050082, 0x00000006, 0x00000A8B, 0x00000A7D,
    0x00000A8A, 0x00050084, 0x00000006, 0x00000A8F, 0x00000A7F, 0x0000087C,
    0x00050080, 0x00000006, 0x00000A91, 0x00000A8F, 0x00000A7B, 0x00050080,
    0x00000006, 0x00000A95, 0x00000881, 0x00000A91, 0x00050082, 0x00000006,
    0x00000A99, 0x00000A95, 0x00000886, 0x00050086, 0x00000006, 0x00000A9E,
    0x00000A99, 0x00000889, 0x00050084, 0x00000006, 0x00000AA2, 0x00000A9E,
    0x00000889, 0x00050082, 0x00000006, 0x00000AA3, 0x00000A99, 0x00000AA2,
    0x00050084, 0x00000006, 0x00000AA6, 0x00000AA3, 0x000008A1, 0x00050080,
    0x00000006, 0x00000AA8, 0x00000AA6, 0x00000A85, 0x00050084, 0x00000006,
    0x00000AAB, 0x00000A9E, 0x000008A6, 0x00050080, 0x00000006, 0x00000AAD,
    0x00000AAB, 0x00000A8B, 0x000500C7, 0x00000006, 0x00000ABD, 0x00000AAD,
    0x000000DC, 0x000500AB, 0x0000004D, 0x00000ABE, 0x00000ABD, 0x0000010F,
    0x000300F7, 0x00000AC5, 0x00000000, 0x000400FA, 0x00000ABE, 0x00000ABF,
    0x00000AC2, 0x000200F8, 0x00000AC2, 0x00050041, 0x000003FC, 0x00000AC3,
    0x000003FB, 0x00000114, 0x0004003D, 0x00000006, 0x00000AC4, 0x00000AC3,
    0x000200F9, 0x00000AC5, 0x000200F8, 0x00000ABF, 0x00050041, 0x000003FC,
    0x00000AC0, 0x000003FB, 0x0000025F, 0x0004003D, 0x00000006, 0x00000AC1,
    0x00000AC0, 0x000200F9, 0x00000AC5, 0x000200F8, 0x00000AC5, 0x000700F5,
    0x00000006, 0x00001C2D, 0x00000AC1, 0x00000ABF, 0x00000AC4, 0x00000AC2,
    0x0004007C, 0x0000002B, 0x00000A58, 0x00000AA8, 0x000500C2, 0x00000006,
    0x00000A5B, 0x00000AAD, 0x000000DC, 0x0004007C, 0x0000002B, 0x00000A5C,
    0x00000A5B, 0x00050050, 0x00000033, 0x00000A60, 0x00000A58, 0x00000A5C,
    0x0004007C, 0x0000002B, 0x00000A62, 0x00001C2D, 0x0007005F, 0x0000000F,
    0x00000A63, 0x00000843, 0x00000A60, 0x00000040, 0x00000A62, 0x000300F7,
    0x00000B00, 0x00000000, 0x001300FB, 0x000006DA, 0x00000AD6, 0x00000000,
    0x00000ADA, 0x00000001, 0x00000ADA, 0x00000002, 0x00000ADD, 0x0000000A,
    0x00000ADD, 0x00000003, 0x00000AE0, 0x0000000C, 0x00000AE0, 0x00000004,
    0x00000AF3, 0x00000006, 0x00000AFC, 0x000200F8, 0x00000AFC, 0x0007004F,
    0x000003CC, 0x00000AFE, 0x00000A63, 0x00000A63, 0x00000000, 0x00000001,
    0x0006000C, 0x00000006, 0x00000AFF, 0x00000001, 0x0000003A, 0x00000AFE,
    0x000200F9, 0x00000B00, 0x000200F8, 0x00000AF3, 0x00050051, 0x0000000D,
    0x00000AF5, 0x00000A63, 0x00000000, 0x0007000C, 0x0000000D, 0x00000BFD,
    0x00000001, 0x00000028, 0x00000AF5, 0x0000034F, 0x0007000C, 0x0000000D,
    0x00000BFE, 0x00000001, 0x00000025, 0x00000BFD, 0x000000CF, 0x000500BE,
    0x0000004D, 0x00000C00, 0x00000BFE, 0x000000CE, 0x000600A9, 0x0000000D,
    0x00000C01, 0x00000C00, 0x0000010B, 0x00000355, 0x0008000C, 0x0000000D,
    0x00000C05, 0x00000001, 0x00000032, 0x00000BFE, 0x00000358, 0x00000C01,
    0x0004006E, 0x0000002B, 0x00000C06, 0x00000C05, 0x0004007C, 0x00000006,
    0x00000C07, 0x00000C06, 0x000500C7, 0x00000006, 0x00000C08, 0x00000C07,
    0x0000035E, 0x00050051, 0x0000000D, 0x00000AF8, 0x00000A63, 0x00000001,
    0x0007000C, 0x0000000D, 0x00000C0E, 0x00000001, 0x00000028, 0x00000AF8,
    0x0000034F, 0x0007000C, 0x0000000D, 0x00000C0F, 0x00000001, 0x00000025,
    0x00000C0E, 0x000000CF, 0x000500BE, 0x0000004D, 0x00000C11, 0x00000C0F,
    0x000000CE, 0x000600A9, 0x0000000D, 0x00000C12, 0x00000C11, 0x0000010B,
    0x00000355, 0x0008000C, 0x0000000D, 0x00000C16, 0x00000001, 0x00000032,
    0x00000C0F, 0x00000358, 0x00000C12, 0x0004006E, 0x0000002B, 0x00000C17,
    0x00000C16, 0x0004007C, 0x00000006, 0x00000C18, 0x00000C17, 0x000500C7,
    0x00000006, 0x00000C19, 0x00000C18, 0x0000035E, 0x000500C4, 0x00000006,
    0x00000AFA, 0x00000C19, 0x000000FB, 0x000500C5, 0x00000006, 0x00000AFB,
    0x00000C08, 0x00000AFA, 0x000200F9, 0x00000B00, 0x000200F8, 0x00000AE0,
    0x00050051, 0x0000000D, 0x00000AE2, 0x00000A63, 0x00000000, 0x0007000C,
    0x0000000D, 0x00000B65, 0x00000001, 0x00000028, 0x00000AE2, 0x000000CE,
    0x0007000C, 0x0000000D, 0x00000B66, 0x00000001, 0x00000025, 0x00000B65,
    0x00000392, 0x0004007C, 0x00000006, 0x00000B72, 0x00000B66, 0x000500B0,
    0x0000004D, 0x00000B74, 0x00000B72, 0x00000366, 0x000300F7, 0x00000B84,
    0x00000000, 0x000400FA, 0x00000B74, 0x00000B75, 0x00000B81, 0x000200F8,
    0x00000B81, 0x00050080, 0x00000006, 0x00000B83, 0x00000B72, 0x0000037F,
    0x000200F9, 0x00000B84, 0x000200F8, 0x00000B75, 0x000500C2, 0x00000006,
    0x00000B77, 0x00000B72, 0x0000036C, 0x00050082, 0x00000006, 0x00000B79,
    0x0000036F, 0x00000B77, 0x0007000C, 0x00000006, 0x00000B7A, 0x00000001,
    0x00000026, 0x00000B79, 0x0000022A, 0x000500C7, 0x00000006, 0x00000B7C,
    0x00000B72, 0x00000375, 0x000500C5, 0x00000006, 0x00000B7D, 0x00000B7C,
    0x00000377, 0x000500C2, 0x00000006, 0x00000B80, 0x00000B7D, 0x00000B7A,
    0x000200F9, 0x00000B84, 0x000200F8, 0x00000B84, 0x000700F5, 0x00000006,
    0x00001C2E, 0x00000B80, 0x00000B75, 0x00000B83, 0x00000B81, 0x000500C2,
    0x00000006, 0x00000B86, 0x00001C2E, 0x000000FB, 0x000500C7, 0x00000006,
    0x00000B87, 0x00000B86, 0x000000DC, 0x00050080, 0x00000006, 0x00000B89,
    0x00001C2E, 0x00000387, 0x00050080, 0x00000006, 0x00000B8B, 0x00000B89,
    0x00000B87, 0x000500C2, 0x00000006, 0x00000B8D, 0x00000B8B, 0x000000FB,
    0x000500C7, 0x00000006, 0x00000B8E, 0x00000B8D, 0x00000215, 0x00050051,
    0x0000000D, 0x00000AE5, 0x00000A63, 0x00000001, 0x0007000C, 0x0000000D,
    0x00000B93, 0x00000001, 0x00000028, 0x00000AE5, 0x000000CE, 0x0007000C,
    0x0000000D, 0x00000B94, 0x00000001, 0x00000025, 0x00000B93, 0x00000392,
    0x0004007C, 0x00000006, 0x00000BA0, 0x00000B94, 0x000500B0, 0x0000004D,
    0x00000BA2, 0x00000BA0, 0x00000366, 0x000300F7, 0x00000BB2, 0x00000000,
    0x000400FA, 0x00000BA2, 0x00000BA3, 0x00000BAF, 0x000200F8, 0x00000BAF,
    0x00050080, 0x00000006, 0x00000BB1, 0x00000BA0, 0x0000037F, 0x000200F9,
    0x00000BB2, 0x000200F8, 0x00000BA3, 0x000500C2, 0x00000006, 0x00000BA5,
    0x00000BA0, 0x0000036C, 0x00050082, 0x00000006, 0x00000BA7, 0x0000036F,
    0x00000BA5, 0x0007000C, 0x00000006, 0x00000BA8, 0x00000001, 0x00000026,
    0x00000BA7, 0x0000022A, 0x000500C7, 0x00000006, 0x00000BAA, 0x00000BA0,
    0x00000375, 0x000500C5, 0x00000006, 0x00000BAB, 0x00000BAA, 0x00000377,
    0x000500C2, 0x00000006, 0x00000BAE, 0x00000BAB, 0x00000BA8, 0x000200F9,
    0x00000BB2, 0x000200F8, 0x00000BB2, 0x000700F5, 0x00000006, 0x00001C2F,
    0x00000BAE, 0x00000BA3, 0x00000BB1, 0x00000BAF, 0x000500C2, 0x00000006,
    0x00000BB4, 0x00001C2F, 0x000000FB, 0x000500C7, 0x00000006, 0x00000BB5,
    0x00000BB4, 0x000000DC, 0x00050080, 0x00000006, 0x00000BB7, 0x00001C2F,
    0x00000387, 0x00050080, 0x00000006, 0x00000BB9, 0x00000BB7, 0x00000BB5,
    0x000500C2, 0x00000006, 0x00000BBB, 0x00000BB9, 0x000000FB, 0x000500C7,
    0x00000006, 0x00000BBC, 0x00000BBB, 0x00000215, 0x000500C4, 0x00000006,
    0x00000AE7, 0x00000BBC, 0x00000219, 0x000500C5, 0x00000006, 0x00000AE8,
    0x00000B8E, 0x00000AE7, 0x00050051, 0x0000000D, 0x00000AEA, 0x00000A63,
    0x00000002, 0x0007000C, 0x0000000D, 0x00000BC1, 0x00000001, 0x00000028,
    0x00000AEA, 0x000000CE, 0x0007000C, 0x0000000D, 0x00000BC2, 0x00000001,
    0x00000025, 0x00000BC1, 0x00000392, 0x0004007C, 0x00000006, 0x00000BCE,
    0x00000BC2, 0x000500B0, 0x0000004D, 0x00000BD0, 0x00000BCE, 0x00000366,
    0x000300F7, 0x00000BE0, 0x00000000, 0x000400FA, 0x00000BD0, 0x00000BD1,
    0x00000BDD, 0x000200F8, 0x00000BDD, 0x00050080, 0x00000006, 0x00000BDF,
    0x00000BCE, 0x0000037F, 0x000200F9, 0x00000BE0, 0x000200F8, 0x00000BD1,
    0x000500C2, 0x00000006, 0x00000BD3, 0x00000BCE, 0x0000036C, 0x00050082,
    0x00000006, 0x00000BD5, 0x0000036F, 0x00000BD3, 0x0007000C, 0x00000006,
    0x00000BD6, 0x00000001, 0x00000026, 0x00000BD5, 0x0000022A, 0x000500C7,
    0x00000006, 0x00000BD8, 0x00000BCE, 0x00000375, 0x000500C5, 0x00000006,
    0x00000BD9, 0x00000BD8, 0x00000377, 0x000500C2, 0x00000006, 0x00000BDC,
    0x00000BD9, 0x00000BD6, 0x000200F9, 0x00000BE0, 0x000200F8, 0x00000BE0,
    0x000700F5, 0x00000006, 0x00001C30, 0x00000BDC, 0x00000BD1, 0x00000BDF,
    0x00000BDD, 0x000500C2, 0x00000006, 0x00000BE2, 0x00001C30, 0x000000FB,
    0x000500C7, 0x00000006, 0x00000BE3, 0x00000BE2, 0x000000DC, 0x00050080,
    0x00000006, 0x00000BE5, 0x00001C30, 0x00000387, 0x00050080, 0x00000006,
    0x00000BE7, 0x00000BE5, 0x00000BE3, 0x000500C2, 0x00000006, 0x00000BE9,
    0x00000BE7, 0x000000FB, 0x000500C7, 0x00000006, 0x00000BEA, 0x00000BE9,
    0x00000215, 0x000500C4, 0x00000006, 0x00000AEC, 0x00000BEA, 0x0000029C,
    0x000500C5, 0x00000006, 0x00000AED, 0x00000AE8, 0x00000AEC, 0x00050051,
    0x0000000D, 0x00000AEF, 0x00000A63, 0x00000003, 0x0008000C, 0x0000000D,
    0x00000BF7, 0x00000001, 0x0000002B, 0x00000AEF, 0x000000CE, 0x000000CF,
    0x0008000C, 0x0000000D, 0x00000BF2, 0x00000001, 0x00000032, 0x00000BF7,
    0x00000128, 0x0000010B, 0x0004006D, 0x00000006, 0x00000BF3, 0x00000BF2,
    0x000500C4, 0x00000006, 0x00000AF1, 0x00000BF3, 0x000003BD, 0x000500C5,
    0x00000006, 0x00000AF2, 0x00000AED, 0x00000AF1, 0x000200F9, 0x00000B00,
    0x000200F8, 0x00000ADD, 0x0008000C, 0x0000000F, 0x00000B52, 0x00000001,
    0x0000002B, 0x00000A63, 0x00001D37, 0x00001D38, 0x0008000C, 0x0000000F,
    0x00000B3B, 0x00000001, 0x00000032, 0x00000B52, 0x00000129, 0x00001D39,
    0x0004006D, 0x0000001D, 0x00000B3C, 0x00000B3B, 0x00050051, 0x00000006,
    0x00000B3E, 0x00000B3C, 0x00000000, 0x00050051, 0x00000006, 0x00000B40,
    0x00000B3C, 0x00000001, 0x000500C4, 0x00000006, 0x00000B41, 0x00000B40,
    0x00000132, 0x000500C5, 0x00000006, 0x00000B42, 0x00000B3E, 0x00000B41,
    0x00050051, 0x00000006, 0x00000B44, 0x00000B3C, 0x00000002, 0x000500C4,
    0x00000006, 0x00000B45, 0x00000B44, 0x00000137, 0x000500C5, 0x00000006,
    0x00000B46, 0x00000B42, 0x00000B45, 0x00050051, 0x00000006, 0x00000B48,
    0x00000B3C, 0x00000003, 0x000500C4, 0x00000006, 0x00000B49, 0x00000B48,
    0x0000013C, 0x000500C5, 0x00000006, 0x00000B4A, 0x00000B46, 0x00000B49,
    0x000200F9, 0x00000B00, 0x000200F8, 0x00000ADA, 0x0008000C, 0x0000000F,
    0x00000B24, 0x00000001, 0x0000002B, 0x00000A63, 0x00001D37, 0x00001D38,
    0x0005008E, 0x0000000F, 0x00000B0B, 0x00000B24, 0x00000109, 0x00050081,
    0x0000000F, 0x00000B0D, 0x00000B0B, 0x00001D39, 0x0004006D, 0x0000001D,
    0x00000B0E, 0x00000B0D, 0x00050051, 0x00000006, 0x00000B10, 0x00000B0E,
    0x00000000, 0x00050051, 0x00000006, 0x00000B12, 0x00000B0E, 0x00000001,
    0x000500C4, 0x00000006, 0x00000B13, 0x00000B12, 0x00000114, 0x000500C5,
    0x00000006, 0x00000B14, 0x00000B10, 0x00000B13, 0x00050051, 0x00000006,
    0x00000B16, 0x00000B0E, 0x00000002, 0x000500C4, 0x00000006, 0x00000B17,
    0x00000B16, 0x00000119, 0x000500C5, 0x00000006, 0x00000B18, 0x00000B14,
    0x00000B17, 0x00050051, 0x00000006, 0x00000B1A, 0x00000B0E, 0x00000003,
    0x000500C4, 0x00000006, 0x00000B1B, 0x00000B1A, 0x0000011E, 0x000500C5,
    0x00000006, 0x00000B1C, 0x00000B18, 0x00000B1B, 0x000200F9, 0x00000B00,
    0x000200F8, 0x00000AD6, 0x00050051, 0x0000000D, 0x00000AD8, 0x00000A63,
    0x00000000, 0x0004007C, 0x00000006, 0x00000AD9, 0x00000AD8, 0x000200F9,
    0x00000B00, 0x000200F8, 0x00000B00, 0x000F00F5, 0x00000006, 0x00001C33,
    0x00000AD9, 0x00000AD6, 0x00000B1C, 0x00000ADA, 0x00000B4A, 0x00000ADD,
    0x00000AF2, 0x00000BE0, 0x00000AFB, 0x00000AF3, 0x00000AFF, 0x00000AFC,
    0x000300F7, 0x00000C35, 0x00000000, 0x000400FA, 0x00000808, 0x00000C2D,
    0x00000C2F, 0x000200F8, 0x00000C2F, 0x000500AA, 0x0000004D, 0x00000C31,
    0x0000073D, 0x000001C4, 0x000600A9, 0x00000006, 0x00001D47, 0x00000C31,
    0x000000DF, 0x0000010F, 0x000200F9, 0x00000C35, 0x000200F8, 0x00000C2D,
    0x000200F9, 0x00000C35, 0x000200F8, 0x00000C35, 0x000700F5, 0x00000006,
    0x00001C3F, 0x0000073D, 0x00000C2D, 0x00001D47, 0x00000C2F, 0x00050080,
    0x00000006, 0x00000C3E, 0x00000818, 0x000000DF, 0x00050050, 0x00000008,
    0x00000C44, 0x00000C3E, 0x0000081F, 0x00050080, 0x00000008, 0x00000C47,
    0x00000C44, 0x000006FF, 0x000500C4, 0x00000008, 0x00000C49, 0x00000C47,
    0x000004F0, 0x00050050, 0x00000008, 0x00000C59, 0x00001C3F, 0x00001C3F,
    0x000500C2, 0x00000008, 0x00000C52, 0x00000C59, 0x00000429, 0x000500C7,
    0x00000008, 0x00000C54, 0x00000C52, 0x00001D33, 0x00050080, 0x00000008,
    0x00000C4C, 0x00000C49, 0x00000C54, 0x00050051, 0x00000006, 0x00000C8B,
    0x00000C4C, 0x00000000, 0x00050086, 0x00000006, 0x00000C8D, 0x00000C8B,
    0x000008A1, 0x00050051, 0x00000006, 0x00000C8F, 0x00000C4C, 0x00000001,
    0x00050086, 0x00000006, 0x00000C91, 0x00000C8F, 0x000008A6, 0x00050084,
    0x00000006, 0x00000C96, 0x00000C8D, 0x000008A1, 0x00050082, 0x00000006,
    0x00000C97, 0x00000C8B, 0x00000C96, 0x00050084, 0x00000006, 0x00000C9C,
    0x00000C91, 0x000008A6, 0x00050082, 0x00000006, 0x00000C9D, 0x00000C8F,
    0x00000C9C, 0x00050084, 0x00000006, 0x00000CA1, 0x00000C91, 0x0000087C,
    0x00050080, 0x00000006, 0x00000CA3, 0x00000CA1, 0x00000C8D, 0x00050080,
    0x00000006, 0x00000CA7, 0x00000881, 0x00000CA3, 0x00050082, 0x00000006,
    0x00000CAB, 0x00000CA7, 0x00000886, 0x00050086, 0x00000006, 0x00000CB0,
    0x00000CAB, 0x00000889, 0x00050084, 0x00000006, 0x00000CB4, 0x00000CB0,
    0x00000889, 0x00050082, 0x00000006, 0x00000CB5, 0x00000CAB, 0x00000CB4,
    0x00050084, 0x00000006, 0x00000CB8, 0x00000CB5, 0x000008A1, 0x00050080,
    0x00000006, 0x00000CBA, 0x00000CB8, 0x00000C97, 0x00050084, 0x00000006,
    0x00000CBD, 0x00000CB0, 0x000008A6, 0x00050080, 0x00000006, 0x00000CBF,
    0x00000CBD, 0x00000C9D, 0x000500C7, 0x00000006, 0x00000CCF, 0x00000CBF,
    0x000000DC, 0x000500AB, 0x0000004D, 0x00000CD0, 0x00000CCF, 0x0000010F,
    0x000300F7, 0x00000CD7, 0x00000000, 0x000400FA, 0x00000CD0, 0x00000CD1,
    0x00000CD4, 0x000200F8, 0x00000CD4, 0x00050041, 0x000003FC, 0x00000CD5,
    0x000003FB, 0x00000114, 0x0004003D, 0x00000006, 0x00000CD6, 0x00000CD5,
    0x000200F9, 0x00000CD7, 0x000200F8, 0x00000CD1, 0x00050041, 0x000003FC,
    0x00000CD2, 0x000003FB, 0x0000025F, 0x0004003D, 0x00000006, 0x00000CD3,
    0x00000CD2, 0x000200F9, 0x00000CD7, 0x000200F8, 0x00000CD7, 0x000700F5,
    0x00000006, 0x00001C41, 0x00000CD3, 0x00000CD1, 0x00000CD6, 0x00000CD4,
    0x0004007C, 0x0000002B, 0x00000C6A, 0x00000CBA, 0x000500C2, 0x00000006,
    0x00000C6D, 0x00000CBF, 0x000000DC, 0x0004007C, 0x0000002B, 0x00000C6E,
    0x00000C6D, 0x00050050, 0x00000033, 0x00000C72, 0x00000C6A, 0x00000C6E,
    0x0004007C, 0x0000002B, 0x00000C74, 0x00001C41, 0x0007005F, 0x0000000F,
    0x00000C75, 0x00000843, 0x00000C72, 0x00000040, 0x00000C74, 0x000300F7,
    0x00000D12, 0x00000000, 0x001300FB, 0x000006DA, 0x00000CE8, 0x00000000,
    0x00000CEC, 0x00000001, 0x00000CEC, 0x00000002, 0x00000CEF, 0x0000000A,
    0x00000CEF, 0x00000003, 0x00000CF2, 0x0000000C, 0x00000CF2, 0x00000004,
    0x00000D05, 0x00000006, 0x00000D0E, 0x000200F8, 0x00000D0E, 0x0007004F,
    0x000003CC, 0x00000D10, 0x00000C75, 0x00000C75, 0x00000000, 0x00000001,
    0x0006000C, 0x00000006, 0x00000D11, 0x00000001, 0x0000003A, 0x00000D10,
    0x000200F9, 0x00000D12, 0x000200F8, 0x00000D05, 0x00050051, 0x0000000D,
    0x00000D07, 0x00000C75, 0x00000000, 0x0007000C, 0x0000000D, 0x00000E0F,
    0x00000001, 0x00000028, 0x00000D07, 0x0000034F, 0x0007000C, 0x0000000D,
    0x00000E10, 0x00000001, 0x00000025, 0x00000E0F, 0x000000CF, 0x000500BE,
    0x0000004D, 0x00000E12, 0x00000E10, 0x000000CE, 0x000600A9, 0x0000000D,
    0x00000E13, 0x00000E12, 0x0000010B, 0x00000355, 0x0008000C, 0x0000000D,
    0x00000E17, 0x00000001, 0x00000032, 0x00000E10, 0x00000358, 0x00000E13,
    0x0004006E, 0x0000002B, 0x00000E18, 0x00000E17, 0x0004007C, 0x00000006,
    0x00000E19, 0x00000E18, 0x000500C7, 0x00000006, 0x00000E1A, 0x00000E19,
    0x0000035E, 0x00050051, 0x0000000D, 0x00000D0A, 0x00000C75, 0x00000001,
    0x0007000C, 0x0000000D, 0x00000E20, 0x00000001, 0x00000028, 0x00000D0A,
    0x0000034F, 0x0007000C, 0x0000000D, 0x00000E21, 0x00000001, 0x00000025,
    0x00000E20, 0x000000CF, 0x000500BE, 0x0000004D, 0x00000E23, 0x00000E21,
    0x000000CE, 0x000600A9, 0x0000000D, 0x00000E24, 0x00000E23, 0x0000010B,
    0x00000355, 0x0008000C, 0x0000000D, 0x00000E28, 0x00000001, 0x00000032,
    0x00000E21, 0x00000358, 0x00000E24, 0x0004006E, 0x0000002B, 0x00000E29,
    0x00000E28, 0x0004007C, 0x00000006, 0x00000E2A, 0x00000E29, 0x000500C7,
    0x00000006, 0x00000E2B, 0x00000E2A, 0x0000035E, 0x000500C4, 0x00000006,
    0x00000D0C, 0x00000E2B, 0x000000FB, 0x000500C5, 0x00000006, 0x00000D0D,
    0x00000E1A, 0x00000D0C, 0x000200F9, 0x00000D12, 0x000200F8, 0x00000CF2,
    0x00050051, 0x0000000D, 0x00000CF4, 0x00000C75, 0x00000000, 0x0007000C,
    0x0000000D, 0x00000D77, 0x00000001, 0x00000028, 0x00000CF4, 0x000000CE,
    0x0007000C, 0x0000000D, 0x00000D78, 0x00000001, 0x00000025, 0x00000D77,
    0x00000392, 0x0004007C, 0x00000006, 0x00000D84, 0x00000D78, 0x000500B0,
    0x0000004D, 0x00000D86, 0x00000D84, 0x00000366, 0x000300F7, 0x00000D96,
    0x00000000, 0x000400FA, 0x00000D86, 0x00000D87, 0x00000D93, 0x000200F8,
    0x00000D93, 0x00050080, 0x00000006, 0x00000D95, 0x00000D84, 0x0000037F,
    0x000200F9, 0x00000D96, 0x000200F8, 0x00000D87, 0x000500C2, 0x00000006,
    0x00000D89, 0x00000D84, 0x0000036C, 0x00050082, 0x00000006, 0x00000D8B,
    0x0000036F, 0x00000D89, 0x0007000C, 0x00000006, 0x00000D8C, 0x00000001,
    0x00000026, 0x00000D8B, 0x0000022A, 0x000500C7, 0x00000006, 0x00000D8E,
    0x00000D84, 0x00000375, 0x000500C5, 0x00000006, 0x00000D8F, 0x00000D8E,
    0x00000377, 0x000500C2, 0x00000006, 0x00000D92, 0x00000D8F, 0x00000D8C,
    0x000200F9, 0x00000D96, 0x000200F8, 0x00000D96, 0x000700F5, 0x00000006,
    0x00001C42, 0x00000D92, 0x00000D87, 0x00000D95, 0x00000D93, 0x000500C2,
    0x00000006, 0x00000D98, 0x00001C42, 0x000000FB, 0x000500C7, 0x00000006,
    0x00000D99, 0x00000D98, 0x000000DC, 0x00050080, 0x00000006, 0x00000D9B,
    0x00001C42, 0x00000387, 0x00050080, 0x00000006, 0x00000D9D, 0x00000D9B,
    0x00000D99, 0x000500C2, 0x00000006, 0x00000D9F, 0x00000D9D, 0x000000FB,
    0x000500C7, 0x00000006, 0x00000DA0, 0x00000D9F, 0x00000215, 0x00050051,
    0x0000000D, 0x00000CF7, 0x00000C75, 0x00000001, 0x0007000C, 0x0000000D,
    0x00000DA5, 0x00000001, 0x00000028, 0x00000CF7, 0x000000CE, 0x0007000C,
    0x0000000D, 0x00000DA6, 0x00000001, 0x00000025, 0x00000DA5, 0x00000392,
    0x0004007C, 0x00000006, 0x00000DB2, 0x00000DA6, 0x000500B0, 0x0000004D,
    0x00000DB4, 0x00000DB2, 0x00000366, 0x000300F7, 0x00000DC4, 0x00000000,
    0x000400FA, 0x00000DB4, 0x00000DB5, 0x00000DC1, 0x000200F8, 0x00000DC1,
    0x00050080, 0x00000006, 0x00000DC3, 0x00000DB2, 0x0000037F, 0x000200F9,
    0x00000DC4, 0x000200F8, 0x00000DB5, 0x000500C2, 0x00000006, 0x00000DB7,
    0x00000DB2, 0x0000036C, 0x00050082, 0x00000006, 0x00000DB9, 0x0000036F,
    0x00000DB7, 0x0007000C, 0x00000006, 0x00000DBA, 0x00000001, 0x00000026,
    0x00000DB9, 0x0000022A, 0x000500C7, 0x00000006, 0x00000DBC, 0x00000DB2,
    0x00000375, 0x000500C5, 0x00000006, 0x00000DBD, 0x00000DBC, 0x00000377,
    0x000500C2, 0x00000006, 0x00000DC0, 0x00000DBD, 0x00000DBA, 0x000200F9,
    0x00000DC4, 0x000200F8, 0x00000DC4, 0x000700F5, 0x00000006, 0x00001C43,
    0x00000DC0, 0x00000DB5, 0x00000DC3, 0x00000DC1, 0x000500C2, 0x00000006,
    0x00000DC6, 0x00001C43, 0x000000FB, 0x000500C7, 0x00000006, 0x00000DC7,
    0x00000DC6, 0x000000DC, 0x00050080, 0x00000006, 0x00000DC9, 0x00001C43,
    0x00000387, 0x00050080, 0x00000006, 0x00000DCB, 0x00000DC9, 0x00000DC7,
    0x000500C2, 0x00000006, 0x00000DCD, 0x00000DCB, 0x000000FB, 0x000500C7,
    0x00000006, 0x00000DCE, 0x00000DCD, 0x00000215, 0x000500C4, 0x00000006,
    0x00000CF9, 0x00000DCE, 0x00000219, 0x000500C5, 0x00000006, 0x00000CFA,
    0x00000DA0, 0x00000CF9, 0x00050051, 0x0000000D, 0x00000CFC, 0x00000C75,
    0x00000002, 0x0007000C, 0x0000000D, 0x00000DD3, 0x00000001, 0x00000028,
    0x00000CFC, 0x000000CE, 0x0007000C, 0x0000000D, 0x00000DD4, 0x00000001,
    0x00000025, 0x00000DD3, 0x00000392, 0x0004007C, 0x00000006, 0x00000DE0,
    0x00000DD4, 0x000500B0, 0x0000004D, 0x00000DE2, 0x00000DE0, 0x00000366,
    0x000300F7, 0x00000DF2, 0x00000000, 0x000400FA, 0x00000DE2, 0x00000DE3,
    0x00000DEF, 0x000200F8, 0x00000DEF, 0x00050080, 0x00000006, 0x00000DF1,
    0x00000DE0, 0x0000037F, 0x000200F9, 0x00000DF2, 0x000200F8, 0x00000DE3,
    0x000500C2, 0x00000006, 0x00000DE5, 0x00000DE0, 0x0000036C, 0x00050082,
    0x00000006, 0x00000DE7, 0x0000036F, 0x00000DE5, 0x0007000C, 0x00000006,
    0x00000DE8, 0x00000001, 0x00000026, 0x00000DE7, 0x0000022A, 0x000500C7,
    0x00000006, 0x00000DEA, 0x00000DE0, 0x00000375, 0x000500C5, 0x00000006,
    0x00000DEB, 0x00000DEA, 0x00000377, 0x000500C2, 0x00000006, 0x00000DEE,
    0x00000DEB, 0x00000DE8, 0x000200F9, 0x00000DF2, 0x000200F8, 0x00000DF2,
    0x000700F5, 0x00000006, 0x00001C44, 0x00000DEE, 0x00000DE3, 0x00000DF1,
    0x00000DEF, 0x000500C2, 0x00000006, 0x00000DF4, 0x00001C44, 0x000000FB,
    0x000500C7, 0x00000006, 0x00000DF5, 0x00000DF4, 0x000000DC, 0x00050080,
    0x00000006, 0x00000DF7, 0x00001C44, 0x00000387, 0x00050080, 0x00000006,
    0x00000DF9, 0x00000DF7, 0x00000DF5, 0x000500C2, 0x00000006, 0x00000DFB,
    0x00000DF9, 0x000000FB, 0x000500C7, 0x00000006, 0x00000DFC, 0x00000DFB,
    0x00000215, 0x000500C4, 0x00000006, 0x00000CFE, 0x00000DFC, 0x0000029C,
    0x000500C5, 0x00000006, 0x00000CFF, 0x00000CFA, 0x00000CFE, 0x00050051,
    0x0000000D, 0x00000D01, 0x00000C75, 0x00000003, 0x0008000C, 0x0000000D,
    0x00000E09, 0x00000001, 0x0000002B, 0x00000D01, 0x000000CE, 0x000000CF,
    0x0008000C, 0x0000000D, 0x00000E04, 0x00000001, 0x00000032, 0x00000E09,
    0x00000128, 0x0000010B, 0x0004006D, 0x00000006, 0x00000E05, 0x00000E04,
    0x000500C4, 0x00000006, 0x00000D03, 0x00000E05, 0x000003BD, 0x000500C5,
    0x00000006, 0x00000D04, 0x00000CFF, 0x00000D03, 0x000200F9, 0x00000D12,
    0x000200F8, 0x00000CEF, 0x0008000C, 0x0000000F, 0x00000D64, 0x00000001,
    0x0000002B, 0x00000C75, 0x00001D37, 0x00001D38, 0x0008000C, 0x0000000F,
    0x00000D4D, 0x00000001, 0x00000032, 0x00000D64, 0x00000129, 0x00001D39,
    0x0004006D, 0x0000001D, 0x00000D4E, 0x00000D4D, 0x00050051, 0x00000006,
    0x00000D50, 0x00000D4E, 0x00000000, 0x00050051, 0x00000006, 0x00000D52,
    0x00000D4E, 0x00000001, 0x000500C4, 0x00000006, 0x00000D53, 0x00000D52,
    0x00000132, 0x000500C5, 0x00000006, 0x00000D54, 0x00000D50, 0x00000D53,
    0x00050051, 0x00000006, 0x00000D56, 0x00000D4E, 0x00000002, 0x000500C4,
    0x00000006, 0x00000D57, 0x00000D56, 0x00000137, 0x000500C5, 0x00000006,
    0x00000D58, 0x00000D54, 0x00000D57, 0x00050051, 0x00000006, 0x00000D5A,
    0x00000D4E, 0x00000003, 0x000500C4, 0x00000006, 0x00000D5B, 0x00000D5A,
    0x0000013C, 0x000500C5, 0x00000006, 0x00000D5C, 0x00000D58, 0x00000D5B,
    0x000200F9, 0x00000D12, 0x000200F8, 0x00000CEC, 0x0008000C, 0x0000000F,
    0x00000D36, 0x00000001, 0x0000002B, 0x00000C75, 0x00001D37, 0x00001D38,
    0x0005008E, 0x0000000F, 0x00000D1D, 0x00000D36, 0x00000109, 0x00050081,
    0x0000000F, 0x00000D1F, 0x00000D1D, 0x00001D39, 0x0004006D, 0x0000001D,
    0x00000D20, 0x00000D1F, 0x00050051, 0x00000006, 0x00000D22, 0x00000D20,
    0x00000000, 0x00050051, 0x00000006, 0x00000D24, 0x00000D20, 0x00000001,
    0x000500C4, 0x00000006, 0x00000D25, 0x00000D24, 0x00000114, 0x000500C5,
    0x00000006, 0x00000D26, 0x00000D22, 0x00000D25, 0x00050051, 0x00000006,
    0x00000D28, 0x00000D20, 0x00000002, 0x000500C4, 0x00000006, 0x00000D29,
    0x00000D28, 0x00000119, 0x000500C5, 0x00000006, 0x00000D2A, 0x00000D26,
    0x00000D29, 0x00050051, 0x00000006, 0x00000D2C, 0x00000D20, 0x00000003,
    0x000500C4, 0x00000006, 0x00000D2D, 0x00000D2C, 0x0000011E, 0x000500C5,
    0x00000006, 0x00000D2E, 0x00000D2A, 0x00000D2D, 0x000200F9, 0x00000D12,
    0x000200F8, 0x00000CE8, 0x00050051, 0x0000000D, 0x00000CEA, 0x00000C75,
    0x00000000, 0x0004007C, 0x00000006, 0x00000CEB, 0x00000CEA, 0x000200F9,
    0x00000D12, 0x000200F8, 0x00000D12, 0x000F00F5, 0x00000006, 0x00001C47,
    0x00000CEB, 0x00000CE8, 0x00000D2E, 0x00000CEC, 0x00000D5C, 0x00000CEF,
    0x00000D04, 0x00000DF2, 0x00000D0D, 0x00000D05, 0x00000D11, 0x00000D0E,
    0x000300F7, 0x00000E47, 0x00000000, 0x000400FA, 0x00000808, 0x00000E3F,
    0x00000E41, 0x000200F8, 0x00000E41, 0x000500AA, 0x0000004D, 0x00000E43,
    0x0000073D, 0x000001C4, 0x000600A9, 0x00000006, 0x00001D48, 0x00000E43,
    0x000000DF, 0x0000010F, 0x000200F9, 0x00000E47, 0x000200F8, 0x00000E3F,
    0x000200F9, 0x00000E47, 0x000200F8, 0x00000E47, 0x000700F5, 0x00000006,
    0x00001C53, 0x0000073D, 0x00000E3F, 0x00001D48, 0x00000E41, 0x00050080,
    0x00000006, 0x00000E50, 0x00000818, 0x000000F5, 0x00050050, 0x00000008,
    0x00000E56, 0x00000E50, 0x0000081F, 0x00050080, 0x00000008, 0x00000E59,
    0x00000E56, 0x000006FF, 0x000500C4, 0x00000008, 0x00000E5B, 0x00000E59,
    0x000004F0, 0x00050050, 0x00000008, 0x00000E6B, 0x00001C53, 0x00001C53,
    0x000500C2, 0x00000008, 0x00000E64, 0x00000E6B, 0x00000429, 0x000500C7,
    0x00000008, 0x00000E66, 0x00000E64, 0x00001D33, 0x00050080, 0x00000008,
    0x00000E5E, 0x00000E5B, 0x00000E66, 0x00050051, 0x00000006, 0x00000E9D,
    0x00000E5E, 0x00000000, 0x00050086, 0x00000006, 0x00000E9F, 0x00000E9D,
    0x000008A1, 0x00050051, 0x00000006, 0x00000EA1, 0x00000E5E, 0x00000001,
    0x00050086, 0x00000006, 0x00000EA3, 0x00000EA1, 0x000008A6, 0x00050084,
    0x00000006, 0x00000EA8, 0x00000E9F, 0x000008A1, 0x00050082, 0x00000006,
    0x00000EA9, 0x00000E9D, 0x00000EA8, 0x00050084, 0x00000006, 0x00000EAE,
    0x00000EA3, 0x000008A6, 0x00050082, 0x00000006, 0x00000EAF, 0x00000EA1,
    0x00000EAE, 0x00050084, 0x00000006, 0x00000EB3, 0x00000EA3, 0x0000087C,
    0x00050080, 0x00000006, 0x00000EB5, 0x00000EB3, 0x00000E9F, 0x00050080,
    0x00000006, 0x00000EB9, 0x00000881, 0x00000EB5, 0x00050082, 0x00000006,
    0x00000EBD, 0x00000EB9, 0x00000886, 0x00050086, 0x00000006, 0x00000EC2,
    0x00000EBD, 0x00000889, 0x00050084, 0x00000006, 0x00000EC6, 0x00000EC2,
    0x00000889, 0x00050082, 0x00000006, 0x00000EC7, 0x00000EBD, 0x00000EC6,
    0x00050084, 0x00000006, 0x00000ECA, 0x00000EC7, 0x000008A1, 0x00050080,
    0x00000006, 0x00000ECC, 0x00000ECA, 0x00000EA9, 0x00050084, 0x00000006,
    0x00000ECF, 0x00000EC2, 0x000008A6, 0x00050080, 0x00000006, 0x00000ED1,
    0x00000ECF, 0x00000EAF, 0x000500C7, 0x00000006, 0x00000EE1, 0x00000ED1,
    0x000000DC, 0x000500AB, 0x0000004D, 0x00000EE2, 0x00000EE1, 0x0000010F,
    0x000300F7, 0x00000EE9, 0x00000000, 0x000400FA, 0x00000EE2, 0x00000EE3,
    0x00000EE6, 0x000200F8, 0x00000EE6, 0x00050041, 0x000003FC, 0x00000EE7,
    0x000003FB, 0x00000114, 0x0004003D, 0x00000006, 0x00000EE8, 0x00000EE7,
    0x000200F9, 0x00000EE9, 0x000200F8, 0x00000EE3, 0x00050041, 0x000003FC,
    0x00000EE4, 0x000003FB, 0x0000025F, 0x0004003D, 0x00000006, 0x00000EE5,
    0x00000EE4, 0x000200F9, 0x00000EE9, 0x000200F8, 0x00000EE9, 0x000700F5,
    0x00000006, 0x00001C55, 0x00000EE5, 0x00000EE3, 0x00000EE8, 0x00000EE6,
    0x0004007C, 0x0000002B, 0x00000E7C, 0x00000ECC, 0x000500C2, 0x00000006,
    0x00000E7F, 0x00000ED1, 0x000000DC, 0x0004007C, 0x0000002B, 0x00000E80,
    0x00000E7F, 0x00050050, 0x00000033, 0x00000E84, 0x00000E7C, 0x00000E80,
    0x0004007C, 0x0000002B, 0x00000E86, 0x00001C55, 0x0007005F, 0x0000000F,
    0x00000E87, 0x00000843, 0x00000E84, 0x00000040, 0x00000E86, 0x000300F7,
    0x00000F24, 0x00000000, 0x001300FB, 0x000006DA, 0x00000EFA, 0x00000000,
    0x00000EFE, 0x00000001, 0x00000EFE, 0x00000002, 0x00000F01, 0x0000000A,
    0x00000F01, 0x00000003, 0x00000F04, 0x0000000C, 0x00000F04, 0x00000004,
    0x00000F17, 0x00000006, 0x00000F20, 0x000200F8, 0x00000F20, 0x0007004F,
    0x000003CC, 0x00000F22, 0x00000E87, 0x00000E87, 0x00000000, 0x00000001,
    0x0006000C, 0x00000006, 0x00000F23, 0x00000001, 0x0000003A, 0x00000F22,
    0x000200F9, 0x00000F24, 0x000200F8, 0x00000F17, 0x00050051, 0x0000000D,
    0x00000F19, 0x00000E87, 0x00000000, 0x0007000C, 0x0000000D, 0x00001021,
    0x00000001, 0x00000028, 0x00000F19, 0x0000034F, 0x0007000C, 0x0000000D,
    0x00001022, 0x00000001, 0x00000025, 0x00001021, 0x000000CF, 0x000500BE,
    0x0000004D, 0x00001024, 0x00001022, 0x000000CE, 0x000600A9, 0x0000000D,
    0x00001025, 0x00001024, 0x0000010B, 0x00000355, 0x0008000C, 0x0000000D,
    0x00001029, 0x00000001, 0x00000032, 0x00001022, 0x00000358, 0x00001025,
    0x0004006E, 0x0000002B, 0x0000102A, 0x00001029, 0x0004007C, 0x00000006,
    0x0000102B, 0x0000102A, 0x000500C7, 0x00000006, 0x0000102C, 0x0000102B,
    0x0000035E, 0x00050051, 0x0000000D, 0x00000F1C, 0x00000E87, 0x00000001,
    0x0007000C, 0x0000000D, 0x00001032, 0x00000001, 0x00000028, 0x00000F1C,
    0x0000034F, 0x0007000C, 0x0000000D, 0x00001033, 0x00000001, 0x00000025,
    0x00001032, 0x000000CF, 0x000500BE, 0x0000004D, 0x00001035, 0x00001033,
    0x000000CE, 0x000600A9, 0x0000000D, 0x00001036, 0x00001035, 0x0000010B,
    0x00000355, 0x0008000C, 0x0000000D, 0x0000103A, 0x00000001, 0x00000032,
    0x00001033, 0x00000358, 0x00001036, 0x0004006E, 0x0000002B, 0x0000103B,
    0x0000103A, 0x0004007C, 0x00000006, 0x0000103C, 0x0000103B, 0x000500C7,
    0x00000006, 0x0000103D, 0x0000103C, 0x0000035E, 0x000500C4, 0x00000006,
    0x00000F1E, 0x0000103D, 0x000000FB, 0x000500C5, 0x00000006, 0x00000F1F,
    0x0000102C, 0x00000F1E, 0x000200F9, 0x00000F24, 0x000200F8, 0x00000F04,
    0x00050051, 0x0000000D, 0x00000F06, 0x00000E87, 0x00000000, 0x0007000C,
    0x0000000D, 0x00000F89, 0x00000001, 0x00000028, 0x00000F06, 0x000000CE,
    0x0007000C, 0x0000000D, 0x00000F8A, 0x00000001, 0x00000025, 0x00000F89,
    0x00000392, 0x0004007C, 0x00000006, 0x00000F96, 0x00000F8A, 0x000500B0,
    0x0000004D, 0x00000F98, 0x00000F96, 0x00000366, 0x000300F7, 0x00000FA8,
    0x00000000, 0x000400FA, 0x00000F98, 0x00000F99, 0x00000FA5, 0x000200F8,
    0x00000FA5, 0x00050080, 0x00000006, 0x00000FA7, 0x00000F96, 0x0000037F,
    0x000200F9, 0x00000FA8, 0x000200F8, 0x00000F99, 0x000500C2, 0x00000006,
    0x00000F9B, 0x00000F96, 0x0000036C, 0x00050082, 0x00000006, 0x00000F9D,
    0x0000036F, 0x00000F9B, 0x0007000C, 0x00000006, 0x00000F9E, 0x00000001,
    0x00000026, 0x00000F9D, 0x0000022A, 0x000500C7, 0x00000006, 0x00000FA0,
    0x00000F96, 0x00000375, 0x000500C5, 0x00000006, 0x00000FA1, 0x00000FA0,
    0x00000377, 0x000500C2, 0x00000006, 0x00000FA4, 0x00000FA1, 0x00000F9E,
    0x000200F9, 0x00000FA8, 0x000200F8, 0x00000FA8, 0x000700F5, 0x00000006,
    0x00001C56, 0x00000FA4, 0x00000F99, 0x00000FA7, 0x00000FA5, 0x000500C2,
    0x00000006, 0x00000FAA, 0x00001C56, 0x000000FB, 0x000500C7, 0x00000006,
    0x00000FAB, 0x00000FAA, 0x000000DC, 0x00050080, 0x00000006, 0x00000FAD,
    0x00001C56, 0x00000387, 0x00050080, 0x00000006, 0x00000FAF, 0x00000FAD,
    0x00000FAB, 0x000500C2, 0x00000006, 0x00000FB1, 0x00000FAF, 0x000000FB,
    0x000500C7, 0x00000006, 0x00000FB2, 0x00000FB1, 0x00000215, 0x00050051,
    0x0000000D, 0x00000F09, 0x00000E87, 0x00000001, 0x0007000C, 0x0000000D,
    0x00000FB7, 0x00000001, 0x00000028, 0x00000F09, 0x000000CE, 0x0007000C,
    0x0000000D, 0x00000FB8, 0x00000001, 0x00000025, 0x00000FB7, 0x00000392,
    0x0004007C, 0x00000006, 0x00000FC4, 0x00000FB8, 0x000500B0, 0x0000004D,
    0x00000FC6, 0x00000FC4, 0x00000366, 0x000300F7, 0x00000FD6, 0x00000000,
    0x000400FA, 0x00000FC6, 0x00000FC7, 0x00000FD3, 0x000200F8, 0x00000FD3,
    0x00050080, 0x00000006, 0x00000FD5, 0x00000FC4, 0x0000037F, 0x000200F9,
    0x00000FD6, 0x000200F8, 0x00000FC7, 0x000500C2, 0x00000006, 0x00000FC9,
    0x00000FC4, 0x0000036C, 0x00050082, 0x00000006, 0x00000FCB, 0x0000036F,
    0x00000FC9, 0x0007000C, 0x00000006, 0x00000FCC, 0x00000001, 0x00000026,
    0x00000FCB, 0x0000022A, 0x000500C7, 0x00000006, 0x00000FCE, 0x00000FC4,
    0x00000375, 0x000500C5, 0x00000006, 0x00000FCF, 0x00000FCE, 0x00000377,
    0x000500C2, 0x00000006, 0x00000FD2, 0x00000FCF, 0x00000FCC, 0x000200F9,
    0x00000FD6, 0x000200F8, 0x00000FD6, 0x000700F5, 0x00000006, 0x00001C57,
    0x00000FD2, 0x00000FC7, 0x00000FD5, 0x00000FD3, 0x000500C2, 0x00000006,
    0x00000FD8, 0x00001C57, 0x000000FB, 0x000500C7, 0x00000006, 0x00000FD9,
    0x00000FD8, 0x000000DC, 0x00050080, 0x00000006, 0x00000FDB, 0x00001C57,
    0x00000387, 0x00050080, 0x00000006, 0x00000FDD, 0x00000FDB, 0x00000FD9,
    0x000500C2, 0x00000006, 0x00000FDF, 0x00000FDD, 0x000000FB, 0x000500C7,
    0x00000006, 0x00000FE0, 0x00000FDF, 0x00000215, 0x000500C4, 0x00000006,
    0x00000F0B, 0x00000FE0, 0x00000219, 0x000500C5, 0x00000006, 0x00000F0C,
    0x00000FB2, 0x00000F0B, 0x00050051, 0x0000000D, 0x00000F0E, 0x00000E87,
    0x00000002, 0x0007000C, 0x0000000D, 0x00000FE5, 0x00000001, 0x00000028,
    0x00000F0E, 0x000000CE, 0x0007000C, 0x0000000D, 0x00000FE6, 0x00000001,
    0x00000025, 0x00000FE5, 0x00000392, 0x0004007C, 0x00000006, 0x00000FF2,
    0x00000FE6, 0x000500B0, 0x0000004D, 0x00000FF4, 0x00000FF2, 0x00000366,
    0x000300F7, 0x00001004, 0x00000000, 0x000400FA, 0x00000FF4, 0x00000FF5,
    0x00001001, 0x000200F8, 0x00001001, 0x00050080, 0x00000006, 0x00001003,
    0x00000FF2, 0x0000037F, 0x000200F9, 0x00001004, 0x000200F8, 0x00000FF5,
    0x000500C2, 0x00000006, 0x00000FF7, 0x00000FF2, 0x0000036C, 0x00050082,
    0x00000006, 0x00000FF9, 0x0000036F, 0x00000FF7, 0x0007000C, 0x00000006,
    0x00000FFA, 0x00000001, 0x00000026, 0x00000FF9, 0x0000022A, 0x000500C7,
    0x00000006, 0x00000FFC, 0x00000FF2, 0x00000375, 0x000500C5, 0x00000006,
    0x00000FFD, 0x00000FFC, 0x00000377, 0x000500C2, 0x00000006, 0x00001000,
    0x00000FFD, 0x00000FFA, 0x000200F9, 0x00001004, 0x000200F8, 0x00001004,
    0x000700F5, 0x00000006, 0x00001C58, 0x00001000, 0x00000FF5, 0x00001003,
    0x00001001, 0x000500C2, 0x00000006, 0x00001006, 0x00001C58, 0x000000FB,
    0x000500C7, 0x00000006, 0x00001007, 0x00001006, 0x000000DC, 0x00050080,
    0x00000006, 0x00001009, 0x00001C58, 0x00000387, 0x00050080, 0x00000006,
    0x0000100B, 0x00001009, 0x00001007, 0x000500C2, 0x00000006, 0x0000100D,
    0x0000100B, 0x000000FB, 0x000500C7, 0x00000006, 0x0000100E, 0x0000100D,
    0x00000215, 0x000500C4, 0x00000006, 0x00000F10, 0x0000100E, 0x0000029C,
    0x000500C5, 0x00000006, 0x00000F11, 0x00000F0C, 0x00000F10, 0x00050051,
    0x0000000D, 0x00000F13, 0x00000E87, 0x00000003, 0x0008000C, 0x0000000D,
    0x0000101B, 0x00000001, 0x0000002B, 0x00000F13, 0x000000CE, 0x000000CF,
    0x0008000C, 0x0000000D, 0x00001016, 0x00000001, 0x00000032, 0x0000101B,
    0x00000128, 0x0000010B, 0x0004006D, 0x00000006, 0x00001017, 0x00001016,
    0x000500C4, 0x00000006, 0x00000F15, 0x00001017, 0x000003BD, 0x000500C5,
    0x00000006, 0x00000F16, 0x00000F11, 0x00000F15, 0x000200F9, 0x00000F24,
    0x000200F8, 0x00000F01, 0x0008000C, 0x0000000F, 0x00000F76, 0x00000001,
    0x0000002B, 0x00000E87, 0x00001D37, 0x00001D38, 0x0008000C, 0x0000000F,
    0x00000F5F, 0x00000001, 0x00000032, 0x00000F76, 0x00000129, 0x00001D39,
    0x0004006D, 0x0000001D, 0x00000F60, 0x00000F5F, 0x00050051, 0x00000006,
    0x00000F62, 0x00000F60, 0x00000000, 0x00050051, 0x00000006, 0x00000F64,
    0x00000F60, 0x00000001, 0x000500C4, 0x00000006, 0x00000F65, 0x00000F64,
    0x00000132, 0x000500C5, 0x00000006, 0x00000F66, 0x00000F62, 0x00000F65,
    0x00050051, 0x00000006, 0x00000F68, 0x00000F60, 0x00000002, 0x000500C4,
    0x00000006, 0x00000F69, 0x00000F68, 0x00000137, 0x000500C5, 0x00000006,
    0x00000F6A, 0x00000F66, 0x00000F69, 0x00050051, 0x00000006, 0x00000F6C,
    0x00000F60, 0x00000003, 0x000500C4, 0x00000006, 0x00000F6D, 0x00000F6C,
    0x0000013C, 0x000500C5, 0x00000006, 0x00000F6E, 0x00000F6A, 0x00000F6D,
    0x000200F9, 0x00000F24, 0x000200F8, 0x00000EFE, 0x0008000C, 0x0000000F,
    0x00000F48, 0x00000001, 0x0000002B, 0x00000E87, 0x00001D37, 0x00001D38,
    0x0005008E, 0x0000000F, 0x00000F2F, 0x00000F48, 0x00000109, 0x00050081,
    0x0000000F, 0x00000F31, 0x00000F2F, 0x00001D39, 0x0004006D, 0x0000001D,
    0x00000F32, 0x00000F31, 0x00050051, 0x00000006, 0x00000F34, 0x00000F32,
    0x00000000, 0x00050051, 0x00000006, 0x00000F36, 0x00000F32, 0x00000001,
    0x000500C4, 0x00000006, 0x00000F37, 0x00000F36, 0x00000114, 0x000500C5,
    0x00000006, 0x00000F38, 0x00000F34, 0x00000F37, 0x00050051, 0x00000006,
    0x00000F3A, 0x00000F32, 0x00000002, 0x000500C4, 0x00000006, 0x00000F3B,
    0x00000F3A, 0x00000119, 0x000500C5, 0x00000006, 0x00000F3C, 0x00000F38,
    0x00000F3B, 0x00050051, 0x00000006, 0x00000F3E, 0x00000F32, 0x00000003,
    0x000500C4, 0x00000006, 0x00000F3F, 0x00000F3E, 0x0000011E, 0x000500C5,
    0x00000006, 0x00000F40, 0x00000F3C, 0x00000F3F, 0x000200F9, 0x00000F24,
    0x000200F8, 0x00000EFA, 0x00050051, 0x0000000D, 0x00000EFC, 0x00000E87,
    0x00000000, 0x0004007C, 0x00000006, 0x00000EFD, 0x00000EFC, 0x000200F9,
    0x00000F24, 0x000200F8, 0x00000F24, 0x000F00F5, 0x00000006, 0x00001C5B,
    0x00000EFD, 0x00000EFA, 0x00000F40, 0x00000EFE, 0x00000F6E, 0x00000F01,
    0x00000F16, 0x00001004, 0x00000F1F, 0x00000F17, 0x00000F23, 0x00000F20,
    0x00070050, 0x0000001D, 0x00001D43, 0x00001C19, 0x00001C33, 0x00001C47,
    0x00001C5B, 0x000300F7, 0x00001059, 0x00000000, 0x000400FA, 0x00000808,
    0x00001051, 0x00001053, 0x000200F8, 0x00001053, 0x000500AA, 0x0000004D,
    0x00001055, 0x0000073D, 0x000001C4, 0x000600A9, 0x00000006, 0x00001D49,
    0x00001055, 0x000000DF, 0x0000010F, 0x000200F9, 0x00001059, 0x000200F8,
    0x00001051, 0x000200F9, 0x00001059, 0x000200F8, 0x00001059, 0x000700F5,
    0x00000006, 0x00001C67, 0x0000073D, 0x00001051, 0x00001D49, 0x00001053,
    0x00050080, 0x00000006, 0x00001062, 0x00000818, 0x000001C7, 0x00050050,
    0x00000008, 0x00001068, 0x00001062, 0x0000081F, 0x00050080, 0x00000008,
    0x0000106B, 0x00001068, 0x000006FF, 0x000500C4, 0x00000008, 0x0000106D,
    0x0000106B, 0x000004F0, 0x00050050, 0x00000008, 0x0000107D, 0x00001C67,
    0x00001C67, 0x000500C2, 0x00000008, 0x00001076, 0x0000107D, 0x00000429,
    0x000500C7, 0x00000008, 0x00001078, 0x00001076, 0x00001D33, 0x00050080,
    0x00000008, 0x00001070, 0x0000106D, 0x00001078, 0x00050051, 0x00000006,
    0x000010AF, 0x00001070, 0x00000000, 0x00050086, 0x00000006, 0x000010B1,
    0x000010AF, 0x000008A1, 0x00050051, 0x00000006, 0x000010B3, 0x00001070,
    0x00000001, 0x00050086, 0x00000006, 0x000010B5, 0x000010B3, 0x000008A6,
    0x00050084, 0x00000006, 0x000010BA, 0x000010B1, 0x000008A1, 0x00050082,
    0x00000006, 0x000010BB, 0x000010AF, 0x000010BA, 0x00050084, 0x00000006,
    0x000010C0, 0x000010B5, 0x000008A6, 0x00050082, 0x00000006, 0x000010C1,
    0x000010B3, 0x000010C0, 0x00050084, 0x00000006, 0x000010C5, 0x000010B5,
    0x0000087C, 0x00050080, 0x00000006, 0x000010C7, 0x000010C5, 0x000010B1,
    0x00050080, 0x00000006, 0x000010CB, 0x00000881, 0x000010C7, 0x00050082,
    0x00000006, 0x000010CF, 0x000010CB, 0x00000886, 0x00050086, 0x00000006,
    0x000010D4, 0x000010CF, 0x00000889, 0x00050084, 0x00000006, 0x000010D8,
    0x000010D4, 0x00000889, 0x00050082, 0x00000006, 0x000010D9, 0x000010CF,
    0x000010D8, 0x00050084, 0x00000006, 0x000010DC, 0x000010D9, 0x000008A1,
    0x00050080, 0x00000006, 0x000010DE, 0x000010DC, 0x000010BB, 0x00050084,
    0x00000006, 0x000010E1, 0x000010D4, 0x000008A6, 0x00050080, 0x00000006,
    0x000010E3, 0x000010E1, 0x000010C1, 0x000500C7, 0x00000006, 0x000010F3,
    0x000010E3, 0x000000DC, 0x000500AB, 0x0000004D, 0x000010F4, 0x000010F3,
    0x0000010F, 0x000300F7, 0x000010FB, 0x00000000, 0x000400FA, 0x000010F4,
    0x000010F5, 0x000010F8, 0x000200F8, 0x000010F8, 0x00050041, 0x000003FC,
    0x000010F9, 0x000003FB, 0x00000114, 0x0004003D, 0x00000006, 0x000010FA,
    0x000010F9, 0x000200F9, 0x000010FB, 0x000200F8, 0x000010F5, 0x00050041,
    0x000003FC, 0x000010F6, 0x000003FB, 0x0000025F, 0x0004003D, 0x00000006,
    0x000010F7, 0x000010F6, 0x000200F9, 0x000010FB, 0x000200F8, 0x000010FB,
    0x000700F5, 0x00000006, 0x00001C69, 0x000010F7, 0x000010F5, 0x000010FA,
    0x000010F8, 0x0004007C, 0x0000002B, 0x0000108E, 0x000010DE, 0x000500C2,
    0x00000006, 0x00001091, 0x000010E3, 0x000000DC, 0x0004007C, 0x0000002B,
    0x00001092, 0x00001091, 0x00050050, 0x00000033, 0x00001096, 0x0000108E,
    0x00001092, 0x0004007C, 0x0000002B, 0x00001098, 0x00001C69, 0x0007005F,
    0x0000000F, 0x00001099, 0x00000843, 0x00001096, 0x00000040, 0x00001098,
    0x000300F7, 0x00001136, 0x00000000, 0x001300FB, 0x000006DA, 0x0000110C,
    0x00000000, 0x00001110, 0x00000001, 0x00001110, 0x00000002, 0x00001113,
    0x0000000A, 0x00001113, 0x00000003, 0x00001116, 0x0000000C, 0x00001116,
    0x00000004, 0x00001129, 0x00000006, 0x00001132, 0x000200F8, 0x00001132,
    0x0007004F, 0x000003CC, 0x00001134, 0x00001099, 0x00001099, 0x00000000,
    0x00000001, 0x0006000C, 0x00000006, 0x00001135, 0x00000001, 0x0000003A,
    0x00001134, 0x000200F9, 0x00001136, 0x000200F8, 0x00001129, 0x00050051,
    0x0000000D, 0x0000112B, 0x00001099, 0x00000000, 0x0007000C, 0x0000000D,
    0x00001233, 0x00000001, 0x00000028, 0x0000112B, 0x0000034F, 0x0007000C,
    0x0000000D, 0x00001234, 0x00000001, 0x00000025, 0x00001233, 0x000000CF,
    0x000500BE, 0x0000004D, 0x00001236, 0x00001234, 0x000000CE, 0x000600A9,
    0x0000000D, 0x00001237, 0x00001236, 0x0000010B, 0x00000355, 0x0008000C,
    0x0000000D, 0x0000123B, 0x00000001, 0x00000032, 0x00001234, 0x00000358,
    0x00001237, 0x0004006E, 0x0000002B, 0x0000123C, 0x0000123B, 0x0004007C,
    0x00000006, 0x0000123D, 0x0000123C, 0x000500C7, 0x00000006, 0x0000123E,
    0x0000123D, 0x0000035E, 0x00050051, 0x0000000D, 0x0000112E, 0x00001099,
    0x00000001, 0x0007000C, 0x0000000D, 0x00001244, 0x00000001, 0x00000028,
    0x0000112E, 0x0000034F, 0x0007000C, 0x0000000D, 0x00001245, 0x00000001,
    0x00000025, 0x00001244, 0x000000CF, 0x000500BE, 0x0000004D, 0x00001247,
    0x00001245, 0x000000CE, 0x000600A9, 0x0000000D, 0x00001248, 0x00001247,
    0x0000010B, 0x00000355, 0x0008000C, 0x0000000D, 0x0000124C, 0x00000001,
    0x00000032, 0x00001245, 0x00000358, 0x00001248, 0x0004006E, 0x0000002B,
    0x0000124D, 0x0000124C, 0x0004007C, 0x00000006, 0x0000124E, 0x0000124D,
    0x000500C7, 0x00000006, 0x0000124F, 0x0000124E, 0x0000035E, 0x000500C4,
    0x00000006, 0x00001130, 0x0000124F, 0x000000FB, 0x000500C5, 0x00000006,
    0x00001131, 0x0000123E, 0x00001130, 0x000200F9, 0x00001136, 0x000200F8,
    0x00001116, 0x00050051, 0x0000000D, 0x00001118, 0x00001099, 0x00000000,
    0x0007000C, 0x0000000D, 0x0000119B, 0x00000001, 0x00000028, 0x00001118,
    0x000000CE, 0x0007000C, 0x0000000D, 0x0000119C, 0x00000001, 0x00000025,
    0x0000119B, 0x00000392, 0x0004007C, 0x00000006, 0x000011A8, 0x0000119C,
    0x000500B0, 0x0000004D, 0x000011AA, 0x000011A8, 0x00000366, 0x000300F7,
    0x000011BA, 0x00000000, 0x000400FA, 0x000011AA, 0x000011AB, 0x000011B7,
    0x000200F8, 0x000011B7, 0x00050080, 0x00000006, 0x000011B9, 0x000011A8,
    0x0000037F, 0x000200F9, 0x000011BA, 0x000200F8, 0x000011AB, 0x000500C2,
    0x00000006, 0x000011AD, 0x000011A8, 0x0000036C, 0x00050082, 0x00000006,
    0x000011AF, 0x0000036F, 0x000011AD, 0x0007000C, 0x00000006, 0x000011B0,
    0x00000001, 0x00000026, 0x000011AF, 0x0000022A, 0x000500C7, 0x00000006,
    0x000011B2, 0x000011A8, 0x00000375, 0x000500C5, 0x00000006, 0x000011B3,
    0x000011B2, 0x00000377, 0x000500C2, 0x00000006, 0x000011B6, 0x000011B3,
    0x000011B0, 0x000200F9, 0x000011BA, 0x000200F8, 0x000011BA, 0x000700F5,
    0x00000006, 0x00001C6A, 0x000011B6, 0x000011AB, 0x000011B9, 0x000011B7,
    0x000500C2, 0x00000006, 0x000011BC, 0x00001C6A, 0x000000FB, 0x000500C7,
    0x00000006, 0x000011BD, 0x000011BC, 0x000000DC, 0x00050080, 0x00000006,
    0x000011BF, 0x00001C6A, 0x00000387, 0x00050080, 0x00000006, 0x000011C1,
    0x000011BF, 0x000011BD, 0x000500C2, 0x00000006, 0x000011C3, 0x000011C1,
    0x000000FB, 0x000500C7, 0x00000006, 0x000011C4, 0x000011C3, 0x00000215,
    0x00050051, 0x0000000D, 0x0000111B, 0x00001099, 0x00000001, 0x0007000C,
    0x0000000D, 0x000011C9, 0x00000001, 0x00000028, 0x0000111B, 0x000000CE,
    0x0007000C, 0x0000000D, 0x000011CA, 0x00000001, 0x00000025, 0x000011C9,
    0x00000392, 0x0004007C, 0x00000006, 0x000011D6, 0x000011CA, 0x000500B0,
    0x0000004D, 0x000011D8, 0x000011D6, 0x00000366, 0x000300F7, 0x000011E8,
    0x00000000, 0x000400FA, 0x000011D8, 0x000011D9, 0x000011E5, 0x000200F8,
    0x000011E5, 0x00050080, 0x00000006, 0x000011E7, 0x000011D6, 0x0000037F,
    0x000200F9, 0x000011E8, 0x000200F8, 0x000011D9, 0x000500C2, 0x00000006,
    0x000011DB, 0x000011D6, 0x0000036C, 0x00050082, 0x00000006, 0x000011DD,
    0x0000036F, 0x000011DB, 0x0007000C, 0x00000006, 0x000011DE, 0x00000001,
    0x00000026, 0x000011DD, 0x0000022A, 0x000500C7, 0x00000006, 0x000011E0,
    0x000011D6, 0x00000375, 0x000500C5, 0x00000006, 0x000011E1, 0x000011E0,
    0x00000377, 0x000500C2, 0x00000006, 0x000011E4, 0x000011E1, 0x000011DE,
    0x000200F9, 0x000011E8, 0x000200F8, 0x000011E8, 0x000700F5, 0x00000006,
    0x00001C6B, 0x000011E4, 0x000011D9, 0x000011E7, 0x000011E5, 0x000500C2,
    0x00000006, 0x000011EA, 0x00001C6B, 0x000000FB, 0x000500C7, 0x00000006,
    0x000011EB, 0x000011EA, 0x000000DC, 0x00050080, 0x00000006, 0x000011ED,
    0x00001C6B, 0x00000387, 0x00050080, 0x00000006, 0x000011EF, 0x000011ED,
    0x000011EB, 0x000500C2, 0x00000006, 0x000011F1, 0x000011EF, 0x000000FB,
    0x000500C7, 0x00000006, 0x000011F2, 0x000011F1, 0x00000215, 0x000500C4,
    0x00000006, 0x0000111D, 0x000011F2, 0x00000219, 0x000500C5, 0x00000006,
    0x0000111E, 0x000011C4, 0x0000111D, 0x00050051, 0x0000000D, 0x00001120,
    0x00001099, 0x00000002, 0x0007000C, 0x0000000D, 0x000011F7, 0x00000001,
    0x00000028, 0x00001120, 0x000000CE, 0x0007000C, 0x0000000D, 0x000011F8,
    0x00000001, 0x00000025, 0x000011F7, 0x00000392, 0x0004007C, 0x00000006,
    0x00001204, 0x000011F8, 0x000500B0, 0x0000004D, 0x00001206, 0x00001204,
    0x00000366, 0x000300F7, 0x00001216, 0x00000000, 0x000400FA, 0x00001206,
    0x00001207, 0x00001213, 0x000200F8, 0x00001213, 0x00050080, 0x00000006,
    0x00001215, 0x00001204, 0x0000037F, 0x000200F9, 0x00001216, 0x000200F8,
    0x00001207, 0x000500C2, 0x00000006, 0x00001209, 0x00001204, 0x0000036C,
    0x00050082, 0x00000006, 0x0000120B, 0x0000036F, 0x00001209, 0x0007000C,
    0x00000006, 0x0000120C, 0x00000001, 0x00000026, 0x0000120B, 0x0000022A,
    0x000500C7, 0x00000006, 0x0000120E, 0x00001204, 0x00000375, 0x000500C5,
    0x00000006, 0x0000120F, 0x0000120E, 0x00000377, 0x000500C2, 0x00000006,
    0x00001212, 0x0000120F, 0x0000120C, 0x000200F9, 0x00001216, 0x000200F8,
    0x00001216, 0x000700F5, 0x00000006, 0x00001C6C, 0x00001212, 0x00001207,
    0x00001215, 0x00001213, 0x000500C2, 0x00000006, 0x00001218, 0x00001C6C,
    0x000000FB, 0x000500C7, 0x00000006, 0x00001219, 0x00001218, 0x000000DC,
    0x00050080, 0x00000006, 0x0000121B, 0x00001C6C, 0x00000387, 0x00050080,
    0x00000006, 0x0000121D, 0x0000121B, 0x00001219, 0x000500C2, 0x00000006,
    0x0000121F, 0x0000121D, 0x000000FB, 0x000500C7, 0x00000006, 0x00001220,
    0x0000121F, 0x00000215, 0x000500C4, 0x00000006, 0x00001122, 0x00001220,
    0x0000029C, 0x000500C5, 0x00000006, 0x00001123, 0x0000111E, 0x00001122,
    0x00050051, 0x0000000D, 0x00001125, 0x00001099, 0x00000003, 0x0008000C,
    0x0000000D, 0x0000122D, 0x00000001, 0x0000002B, 0x00001125, 0x000000CE,
    0x000000CF, 0x0008000C, 0x0000000D, 0x00001228, 0x00000001, 0x00000032,
    0x0000122D, 0x00000128, 0x0000010B, 0x0004006D, 0x00000006, 0x00001229,
    0x00001228, 0x000500C4, 0x00000006, 0x00001127, 0x00001229, 0x000003BD,
    0x000500C5, 0x00000006, 0x00001128, 0x00001123, 0x00001127, 0x000200F9,
    0x00001136, 0x000200F8, 0x00001113, 0x0008000C, 0x0000000F, 0x00001188,
    0x00000001, 0x0000002B, 0x00001099, 0x00001D37, 0x00001D38, 0x0008000C,
    0x0000000F, 0x00001171, 0x00000001, 0x00000032, 0x00001188, 0x00000129,
    0x00001D39, 0x0004006D, 0x0000001D, 0x00001172, 0x00001171, 0x00050051,
    0x00000006, 0x00001174, 0x00001172, 0x00000000, 0x00050051, 0x00000006,
    0x00001176, 0x00001172, 0x00000001, 0x000500C4, 0x00000006, 0x00001177,
    0x00001176, 0x00000132, 0x000500C5, 0x00000006, 0x00001178, 0x00001174,
    0x00001177, 0x00050051, 0x00000006, 0x0000117A, 0x00001172, 0x00000002,
    0x000500C4, 0x00000006, 0x0000117B, 0x0000117A, 0x00000137, 0x000500C5,
    0x00000006, 0x0000117C, 0x00001178, 0x0000117B, 0x00050051, 0x00000006,
    0x0000117E, 0x00001172, 0x00000003, 0x000500C4, 0x00000006, 0x0000117F,
    0x0000117E, 0x0000013C, 0x000500C5, 0x00000006, 0x00001180, 0x0000117C,
    0x0000117F, 0x000200F9, 0x00001136, 0x000200F8, 0x00001110, 0x0008000C,
    0x0000000F, 0x0000115A, 0x00000001, 0x0000002B, 0x00001099, 0x00001D37,
    0x00001D38, 0x0005008E, 0x0000000F, 0x00001141, 0x0000115A, 0x00000109,
    0x00050081, 0x0000000F, 0x00001143, 0x00001141, 0x00001D39, 0x0004006D,
    0x0000001D, 0x00001144, 0x00001143, 0x00050051, 0x00000006, 0x00001146,
    0x00001144, 0x00000000, 0x00050051, 0x00000006, 0x00001148, 0x00001144,
    0x00000001, 0x000500C4, 0x00000006, 0x00001149, 0x00001148, 0x00000114,
    0x000500C5, 0x00000006, 0x0000114A, 0x00001146, 0x00001149, 0x00050051,
    0x00000006, 0x0000114C, 0x00001144, 0x00000002, 0x000500C4, 0x00000006,
    0x0000114D, 0x0000114C, 0x00000119, 0x000500C5, 0x00000006, 0x0000114E,
    0x0000114A, 0x0000114D, 0x00050051, 0x00000006, 0x00001150, 0x00001144,
    0x00000003, 0x000500C4, 0x00000006, 0x00001151, 0x00001150, 0x0000011E,
    0x000500C5, 0x00000006, 0x00001152, 0x0000114E, 0x00001151, 0x000200F9,
    0x00001136, 0x000200F8, 0x0000110C, 0x00050051, 0x0000000D, 0x0000110E,
    0x00001099, 0x00000000, 0x0004007C, 0x00000006, 0x0000110F, 0x0000110E,
    0x000200F9, 0x00001136, 0x000200F8, 0x00001136, 0x000F00F5, 0x00000006,
    0x00001C6F, 0x0000110F, 0x0000110C, 0x00001152, 0x00001110, 0x00001180,
    0x00001113, 0x00001128, 0x00001216, 0x00001131, 0x00001129, 0x00001135,
    0x00001132, 0x000300F7, 0x0000126B, 0x00000000, 0x000400FA, 0x00000808,
    0x00001263, 0x00001265, 0x000200F8, 0x00001265, 0x000500AA, 0x0000004D,
    0x00001267, 0x0000073D, 0x000001C4, 0x000600A9, 0x00000006, 0x00001D4A,
    0x00001267, 0x000000DF, 0x0000010F, 0x000200F9, 0x0000126B, 0x000200F8,
    0x00001263, 0x000200F9, 0x0000126B, 0x000200F8, 0x0000126B, 0x000700F5,
    0x00000006, 0x00001CAC, 0x0000073D, 0x00001263, 0x00001D4A, 0x00001265,
    0x00050080, 0x00000006, 0x00001274, 0x00000818, 0x000001C4, 0x00050050,
    0x00000008, 0x0000127A, 0x00001274, 0x0000081F, 0x00050080, 0x00000008,
    0x0000127D, 0x0000127A, 0x000006FF, 0x000500C4, 0x00000008, 0x0000127F,
    0x0000127D, 0x000004F0, 0x00050050, 0x00000008, 0x0000128F, 0x00001CAC,
    0x00001CAC, 0x000500C2, 0x00000008, 0x00001288, 0x0000128F, 0x00000429,
    0x000500C7, 0x00000008, 0x0000128A, 0x00001288, 0x00001D33, 0x00050080,
    0x00000008, 0x00001282, 0x0000127F, 0x0000128A, 0x00050051, 0x00000006,
    0x000012C1, 0x00001282, 0x00000000, 0x00050086, 0x00000006, 0x000012C3,
    0x000012C1, 0x000008A1, 0x00050051, 0x00000006, 0x000012C5, 0x00001282,
    0x00000001, 0x00050086, 0x00000006, 0x000012C7, 0x000012C5, 0x000008A6,
    0x00050084, 0x00000006, 0x000012CC, 0x000012C3, 0x000008A1, 0x00050082,
    0x00000006, 0x000012CD, 0x000012C1, 0x000012CC, 0x00050084, 0x00000006,
    0x000012D2, 0x000012C7, 0x000008A6, 0x00050082, 0x00000006, 0x000012D3,
    0x000012C5, 0x000012D2, 0x00050084, 0x00000006, 0x000012D7, 0x000012C7,
    0x0000087C, 0x00050080, 0x00000006, 0x000012D9, 0x000012D7, 0x000012C3,
    0x00050080, 0x00000006, 0x000012DD, 0x00000881, 0x000012D9, 0x00050082,
    0x00000006, 0x000012E1, 0x000012DD, 0x00000886, 0x00050086, 0x00000006,
    0x000012E6, 0x000012E1, 0x00000889, 0x00050084, 0x00000006, 0x000012EA,
    0x000012E6, 0x00000889, 0x00050082, 0x00000006, 0x000012EB, 0x000012E1,
    0x000012EA, 0x00050084, 0x00000006, 0x000012EE, 0x000012EB, 0x000008A1,
    0x00050080, 0x00000006, 0x000012F0, 0x000012EE, 0x000012CD, 0x00050084,
    0x00000006, 0x000012F3, 0x000012E6, 0x000008A6, 0x00050080, 0x00000006,
    0x000012F5, 0x000012F3, 0x000012D3, 0x000500C7, 0x00000006, 0x00001305,
    0x000012F5, 0x000000DC, 0x000500AB, 0x0000004D, 0x00001306, 0x00001305,
    0x0000010F, 0x000300F7, 0x0000130D, 0x00000000, 0x000400FA, 0x00001306,
    0x00001307, 0x0000130A, 0x000200F8, 0x0000130A, 0x00050041, 0x000003FC,
    0x0000130B, 0x000003FB, 0x00000114, 0x0004003D, 0x00000006, 0x0000130C,
    0x0000130B, 0x000200F9, 0x0000130D, 0x000200F8, 0x00001307, 0x00050041,
    0x000003FC, 0x00001308, 0x000003FB, 0x0000025F, 0x0004003D, 0x00000006,
    0x00001309, 0x00001308, 0x000200F9, 0x0000130D, 0x000200F8, 0x0000130D,
    0x000700F5, 0x00000006, 0x00001CAE, 0x00001309, 0x00001307, 0x0000130C,
    0x0000130A, 0x0004007C, 0x0000002B, 0x000012A0, 0x000012F0, 0x000500C2,
    0x00000006, 0x000012A3, 0x000012F5, 0x000000DC, 0x0004007C, 0x0000002B,
    0x000012A4, 0x000012A3, 0x00050050, 0x00000033, 0x000012A8, 0x000012A0,
    0x000012A4, 0x0004007C, 0x0000002B, 0x000012AA, 0x00001CAE, 0x0007005F,
    0x0000000F, 0x000012AB, 0x00000843, 0x000012A8, 0x00000040, 0x000012AA,
    0x000300F7, 0x00001348, 0x00000000, 0x001300FB, 0x000006DA, 0x0000131E,
    0x00000000, 0x00001322, 0x00000001, 0x00001322, 0x00000002, 0x00001325,
    0x0000000A, 0x00001325, 0x00000003, 0x00001328, 0x0000000C, 0x00001328,
    0x00000004, 0x0000133B, 0x00000006, 0x00001344, 0x000200F8, 0x00001344,
    0x0007004F, 0x000003CC, 0x00001346, 0x000012AB, 0x000012AB, 0x00000000,
    0x00000001, 0x0006000C, 0x00000006, 0x00001347, 0x00000001, 0x0000003A,
    0x00001346, 0x000200F9, 0x00001348, 0x000200F8, 0x0000133B, 0x00050051,
    0x0000000D, 0x0000133D, 0x000012AB, 0x00000000, 0x0007000C, 0x0000000D,
    0x00001445, 0x00000001, 0x00000028, 0x0000133D, 0x0000034F, 0x0007000C,
    0x0000000D, 0x00001446, 0x00000001, 0x00000025, 0x00001445, 0x000000CF,
    0x000500BE, 0x0000004D, 0x00001448, 0x00001446, 0x000000CE, 0x000600A9,
    0x0000000D, 0x00001449, 0x00001448, 0x0000010B, 0x00000355, 0x0008000C,
    0x0000000D, 0x0000144D, 0x00000001, 0x00000032, 0x00001446, 0x00000358,
    0x00001449, 0x0004006E, 0x0000002B, 0x0000144E, 0x0000144D, 0x0004007C,
    0x00000006, 0x0000144F, 0x0000144E, 0x000500C7, 0x00000006, 0x00001450,
    0x0000144F, 0x0000035E, 0x00050051, 0x0000000D, 0x00001340, 0x000012AB,
    0x00000001, 0x0007000C, 0x0000000D, 0x00001456, 0x00000001, 0x00000028,
    0x00001340, 0x0000034F, 0x0007000C, 0x0000000D, 0x00001457, 0x00000001,
    0x00000025, 0x00001456, 0x000000CF, 0x000500BE, 0x0000004D, 0x00001459,
    0x00001457, 0x000000CE, 0x000600A9, 0x0000000D, 0x0000145A, 0x00001459,
    0x0000010B, 0x00000355, 0x0008000C, 0x0000000D, 0x0000145E, 0x00000001,
    0x00000032, 0x00001457, 0x00000358, 0x0000145A, 0x0004006E, 0x0000002B,
    0x0000145F, 0x0000145E, 0x0004007C, 0x00000006, 0x00001460, 0x0000145F,
    0x000500C7, 0x00000006, 0x00001461, 0x00001460, 0x0000035E, 0x000500C4,
    0x00000006, 0x00001342, 0x00001461, 0x000000FB, 0x000500C5, 0x00000006,
    0x00001343, 0x00001450, 0x00001342, 0x000200F9, 0x00001348, 0x000200F8,
    0x00001328, 0x00050051, 0x0000000D, 0x0000132A, 0x000012AB, 0x00000000,
    0x0007000C, 0x0000000D, 0x000013AD, 0x00000001, 0x00000028, 0x0000132A,
    0x000000CE, 0x0007000C, 0x0000000D, 0x000013AE, 0x00000001, 0x00000025,
    0x000013AD, 0x00000392, 0x0004007C, 0x00000006, 0x000013BA, 0x000013AE,
    0x000500B0, 0x0000004D, 0x000013BC, 0x000013BA, 0x00000366, 0x000300F7,
    0x000013CC, 0x00000000, 0x000400FA, 0x000013BC, 0x000013BD, 0x000013C9,
    0x000200F8, 0x000013C9, 0x00050080, 0x00000006, 0x000013CB, 0x000013BA,
    0x0000037F, 0x000200F9, 0x000013CC, 0x000200F8, 0x000013BD, 0x000500C2,
    0x00000006, 0x000013BF, 0x000013BA, 0x0000036C, 0x00050082, 0x00000006,
    0x000013C1, 0x0000036F, 0x000013BF, 0x0007000C, 0x00000006, 0x000013C2,
    0x00000001, 0x00000026, 0x000013C1, 0x0000022A, 0x000500C7, 0x00000006,
    0x000013C4, 0x000013BA, 0x00000375, 0x000500C5, 0x00000006, 0x000013C5,
    0x000013C4, 0x00000377, 0x000500C2, 0x00000006, 0x000013C8, 0x000013C5,
    0x000013C2, 0x000200F9, 0x000013CC, 0x000200F8, 0x000013CC, 0x000700F5,
    0x00000006, 0x00001CAF, 0x000013C8, 0x000013BD, 0x000013CB, 0x000013C9,
    0x000500C2, 0x00000006, 0x000013CE, 0x00001CAF, 0x000000FB, 0x000500C7,
    0x00000006, 0x000013CF, 0x000013CE, 0x000000DC, 0x00050080, 0x00000006,
    0x000013D1, 0x00001CAF, 0x00000387, 0x00050080, 0x00000006, 0x000013D3,
    0x000013D1, 0x000013CF, 0x000500C2, 0x00000006, 0x000013D5, 0x000013D3,
    0x000000FB, 0x000500C7, 0x00000006, 0x000013D6, 0x000013D5, 0x00000215,
    0x00050051, 0x0000000D, 0x0000132D, 0x000012AB, 0x00000001, 0x0007000C,
    0x0000000D, 0x000013DB, 0x00000001, 0x00000028, 0x0000132D, 0x000000CE,
    0x0007000C, 0x0000000D, 0x000013DC, 0x00000001, 0x00000025, 0x000013DB,
    0x00000392, 0x0004007C, 0x00000006, 0x000013E8, 0x000013DC, 0x000500B0,
    0x0000004D, 0x000013EA, 0x000013E8, 0x00000366, 0x000300F7, 0x000013FA,
    0x00000000, 0x000400FA, 0x000013EA, 0x000013EB, 0x000013F7, 0x000200F8,
    0x000013F7, 0x00050080, 0x00000006, 0x000013F9, 0x000013E8, 0x0000037F,
    0x000200F9, 0x000013FA, 0x000200F8, 0x000013EB, 0x000500C2, 0x00000006,
    0x000013ED, 0x000013E8, 0x0000036C, 0x00050082, 0x00000006, 0x000013EF,
    0x0000036F, 0x000013ED, 0x0007000C, 0x00000006, 0x000013F0, 0x00000001,
    0x00000026, 0x000013EF, 0x0000022A, 0x000500C7, 0x00000006, 0x000013F2,
    0x000013E8, 0x00000375, 0x000500C5, 0x00000006, 0x000013F3, 0x000013F2,
    0x00000377, 0x000500C2, 0x00000006, 0x000013F6, 0x000013F3, 0x000013F0,
    0x000200F9, 0x000013FA, 0x000200F8, 0x000013FA, 0x000700F5, 0x00000006,
    0x00001CB0, 0x000013F6, 0x000013EB, 0x000013F9, 0x000013F7, 0x000500C2,
    0x00000006, 0x000013FC, 0x00001CB0, 0x000000FB, 0x000500C7, 0x00000006,
    0x000013FD, 0x000013FC, 0x000000DC, 0x00050080, 0x00000006, 0x000013FF,
    0x00001CB0, 0x00000387, 0x00050080, 0x00000006, 0x00001401, 0x000013FF,
    0x000013FD, 0x000500C2, 0x00000006, 0x00001403, 0x00001401, 0x000000FB,
    0x000500C7, 0x00000006, 0x00001404, 0x00001403, 0x00000215, 0x000500C4,
    0x00000006, 0x0000132F, 0x00001404, 0x00000219, 0x000500C5, 0x00000006,
    0x00001330, 0x000013D6, 0x0000132F, 0x00050051, 0x0000000D, 0x00001332,
    0x000012AB, 0x00000002, 0x0007000C, 0x0000000D, 0x00001409, 0x00000001,
    0x00000028, 0x00001332, 0x000000CE, 0x0007000C, 0x0000000D, 0x0000140A,
    0x00000001, 0x00000025, 0x00001409, 0x00000392, 0x0004007C, 0x00000006,
    0x00001416, 0x0000140A, 0x000500B0, 0x0000004D, 0x00001418, 0x00001416,
    0x00000366, 0x000300F7, 0x00001428, 0x00000000, 0x000400FA, 0x00001418,
    0x00001419, 0x00001425, 0x000200F8, 0x00001425, 0x00050080, 0x00000006,
    0x00001427, 0x00001416, 0x0000037F, 0x000200F9, 0x00001428, 0x000200F8,
    0x00001419, 0x000500C2, 0x00000006, 0x0000141B, 0x00001416, 0x0000036C,
    0x00050082, 0x00000006, 0x0000141D, 0x0000036F, 0x0000141B, 0x0007000C,
    0x00000006, 0x0000141E, 0x00000001, 0x00000026, 0x0000141D, 0x0000022A,
    0x000500C7, 0x00000006, 0x00001420, 0x00001416, 0x00000375, 0x000500C5,
    0x00000006, 0x00001421, 0x00001420, 0x00000377, 0x000500C2, 0x00000006,
    0x00001424, 0x00001421, 0x0000141E, 0x000200F9, 0x00001428, 0x000200F8,
    0x00001428, 0x000700F5, 0x00000006, 0x00001CB1, 0x00001424, 0x00001419,
    0x00001427, 0x00001425, 0x000500C2, 0x00000006, 0x0000142A, 0x00001CB1,
    0x000000FB, 0x000500C7, 0x00000006, 0x0000142B, 0x0000142A, 0x000000DC,
    0x00050080, 0x00000006, 0x0000142D, 0x00001CB1, 0x00000387, 0x00050080,
    0x00000006, 0x0000142F, 0x0000142D, 0x0000142B, 0x000500C2, 0x00000006,
    0x00001431, 0x0000142F, 0x000000FB, 0x000500C7, 0x00000006, 0x00001432,
    0x00001431, 0x00000215, 0x000500C4, 0x00000006, 0x00001334, 0x00001432,
    0x0000029C, 0x000500C5, 0x00000006, 0x00001335, 0x00001330, 0x00001334,
    0x00050051, 0x0000000D, 0x00001337, 0x000012AB, 0x00000003, 0x0008000C,
    0x0000000D, 0x0000143F, 0x00000001, 0x0000002B, 0x00001337, 0x000000CE,
    0x000000CF, 0x0008000C, 0x0000000D, 0x0000143A, 0x00000001, 0x00000032,
    0x0000143F, 0x00000128, 0x0000010B, 0x0004006D, 0x00000006, 0x0000143B,
    0x0000143A, 0x000500C4, 0x00000006, 0x00001339, 0x0000143B, 0x000003BD,
    0x000500C5, 0x00000006, 0x0000133A, 0x00001335, 0x00001339, 0x000200F9,
    0x00001348, 0x000200F8, 0x00001325, 0x0008000C, 0x0000000F, 0x0000139A,
    0x00000001, 0x0000002B, 0x000012AB, 0x00001D37, 0x00001D38, 0x0008000C,
    0x0000000F, 0x00001383, 0x00000001, 0x00000032, 0x0000139A, 0x00000129,
    0x00001D39, 0x0004006D, 0x0000001D, 0x00001384, 0x00001383, 0x00050051,
    0x00000006, 0x00001386, 0x00001384, 0x00000000, 0x00050051, 0x00000006,
    0x00001388, 0x00001384, 0x00000001, 0x000500C4, 0x00000006, 0x00001389,
    0x00001388, 0x00000132, 0x000500C5, 0x00000006, 0x0000138A, 0x00001386,
    0x00001389, 0x00050051, 0x00000006, 0x0000138C, 0x00001384, 0x00000002,
    0x000500C4, 0x00000006, 0x0000138D, 0x0000138C, 0x00000137, 0x000500C5,
    0x00000006, 0x0000138E, 0x0000138A, 0x0000138D, 0x00050051, 0x00000006,
    0x00001390, 0x00001384, 0x00000003, 0x000500C4, 0x00000006, 0x00001391,
    0x00001390, 0x0000013C, 0x000500C5, 0x00000006, 0x00001392, 0x0000138E,
    0x00001391, 0x000200F9, 0x00001348, 0x000200F8, 0x00001322, 0x0008000C,
    0x0000000F, 0x0000136C, 0x00000001, 0x0000002B, 0x000012AB, 0x00001D37,
    0x00001D38, 0x0005008E, 0x0000000F, 0x00001353, 0x0000136C, 0x00000109,
    0x00050081, 0x0000000F, 0x00001355, 0x00001353, 0x00001D39, 0x0004006D,
    0x0000001D, 0x00001356, 0x00001355, 0x00050051, 0x00000006, 0x00001358,
    0x00001356, 0x00000000, 0x00050051, 0x00000006, 0x0000135A, 0x00001356,
    0x00000001, 0x000500C4, 0x00000006, 0x0000135B, 0x0000135A, 0x00000114,
    0x000500C5, 0x00000006, 0x0000135C, 0x00001358, 0x0000135B, 0x00050051,
    0x00000006, 0x0000135E, 0x00001356, 0x00000002, 0x000500C4, 0x00000006,
    0x0000135F, 0x0000135E, 0x00000119, 0x000500C5, 0x00000006, 0x00001360,
    0x0000135C, 0x0000135F, 0x00050051, 0x00000006, 0x00001362, 0x00001356,
    0x00000003, 0x000500C4, 0x00000006, 0x00001363, 0x00001362, 0x0000011E,
    0x000500C5, 0x00000006, 0x00001364, 0x00001360, 0x00001363, 0x000200F9,
    0x00001348, 0x000200F8, 0x0000131E, 0x00050051, 0x0000000D, 0x00001320,
    0x000012AB, 0x00000000, 0x0004007C, 0x00000006, 0x00001321, 0x00001320,
    0x000200F9, 0x00001348, 0x000200F8, 0x00001348, 0x000F00F5, 0x00000006,
    0x00001CB4, 0x00001321, 0x0000131E, 0x00001364, 0x00001322, 0x00001392,
    0x00001325, 0x0000133A, 0x00001428, 0x00001343, 0x0000133B, 0x00001347,
    0x00001344, 0x000300F7, 0x0000147D, 0x00000000, 0x000400FA, 0x00000808,
    0x00001475, 0x00001477, 0x000200F8, 0x00001477, 0x000500AA, 0x0000004D,
    0x00001479, 0x0000073D, 0x000001C4, 0x000600A9, 0x00000006, 0x00001D4B,
    0x00001479, 0x000000DF, 0x0000010F, 0x000200F9, 0x0000147D, 0x000200F8,
    0x00001475, 0x000200F9, 0x0000147D, 0x000200F8, 0x0000147D, 0x000700F5,
    0x00000006, 0x00001CC0, 0x0000073D, 0x00001475, 0x00001D4B, 0x00001477,
    0x00050080, 0x00000006, 0x00001486, 0x00000818, 0x00000594, 0x00050050,
    0x00000008, 0x0000148C, 0x00001486, 0x0000081F, 0x00050080, 0x00000008,
    0x0000148F, 0x0000148C, 0x000006FF, 0x000500C4, 0x00000008, 0x00001491,
    0x0000148F, 0x000004F0, 0x00050050, 0x00000008, 0x000014A1, 0x00001CC0,
    0x00001CC0, 0x000500C2, 0x00000008, 0x0000149A, 0x000014A1, 0x00000429,
    0x000500C7, 0x00000008, 0x0000149C, 0x0000149A, 0x00001D33, 0x00050080,
    0x00000008, 0x00001494, 0x00001491, 0x0000149C, 0x00050051, 0x00000006,
    0x000014D3, 0x00001494, 0x00000000, 0x00050086, 0x00000006, 0x000014D5,
    0x000014D3, 0x000008A1, 0x00050051, 0x00000006, 0x000014D7, 0x00001494,
    0x00000001, 0x00050086, 0x00000006, 0x000014D9, 0x000014D7, 0x000008A6,
    0x00050084, 0x00000006, 0x000014DE, 0x000014D5, 0x000008A1, 0x00050082,
    0x00000006, 0x000014DF, 0x000014D3, 0x000014DE, 0x00050084, 0x00000006,
    0x000014E4, 0x000014D9, 0x000008A6, 0x00050082, 0x00000006, 0x000014E5,
    0x000014D7, 0x000014E4, 0x00050084, 0x00000006, 0x000014E9, 0x000014D9,
    0x0000087C, 0x00050080, 0x00000006, 0x000014EB, 0x000014E9, 0x000014D5,
    0x00050080, 0x00000006, 0x000014EF, 0x00000881, 0x000014EB, 0x00050082,
    0x00000006, 0x000014F3, 0x000014EF, 0x00000886, 0x00050086, 0x00000006,
    0x000014F8, 0x000014F3, 0x00000889, 0x00050084, 0x00000006, 0x000014FC,
    0x000014F8, 0x00000889, 0x00050082, 0x00000006, 0x000014FD, 0x000014F3,
    0x000014FC, 0x00050084, 0x00000006, 0x00001500, 0x000014FD, 0x000008A1,
    0x00050080, 0x00000006, 0x00001502, 0x00001500, 0x000014DF, 0x00050084,
    0x00000006, 0x00001505, 0x000014F8, 0x000008A6, 0x00050080, 0x00000006,
    0x00001507, 0x00001505, 0x000014E5, 0x000500C7, 0x00000006, 0x00001517,
    0x00001507, 0x000000DC, 0x000500AB, 0x0000004D, 0x00001518, 0x00001517,
    0x0000010F, 0x000300F7, 0x0000151F, 0x00000000, 0x000400FA, 0x00001518,
    0x00001519, 0x0000151C, 0x000200F8, 0x0000151C, 0x00050041, 0x000003FC,
    0x0000151D, 0x000003FB, 0x00000114, 0x0004003D, 0x00000006, 0x0000151E,
    0x0000151D, 0x000200F9, 0x0000151F, 0x000200F8, 0x00001519, 0x00050041,
    0x000003FC, 0x0000151A, 0x000003FB, 0x0000025F, 0x0004003D, 0x00000006,
    0x0000151B, 0x0000151A, 0x000200F9, 0x0000151F, 0x000200F8, 0x0000151F,
    0x000700F5, 0x00000006, 0x00001CC2, 0x0000151B, 0x00001519, 0x0000151E,
    0x0000151C, 0x0004007C, 0x0000002B, 0x000014B2, 0x00001502, 0x000500C2,
    0x00000006, 0x000014B5, 0x00001507, 0x000000DC, 0x0004007C, 0x0000002B,
    0x000014B6, 0x000014B5, 0x00050050, 0x00000033, 0x000014BA, 0x000014B2,
    0x000014B6, 0x0004007C, 0x0000002B, 0x000014BC, 0x00001CC2, 0x0007005F,
    0x0000000F, 0x000014BD, 0x00000843, 0x000014BA, 0x00000040, 0x000014BC,
    0x000300F7, 0x0000155A, 0x00000000, 0x001300FB, 0x000006DA, 0x00001530,
    0x00000000, 0x00001534, 0x00000001, 0x00001534, 0x00000002, 0x00001537,
    0x0000000A, 0x00001537, 0x00000003, 0x0000153A, 0x0000000C, 0x0000153A,
    0x00000004, 0x0000154D, 0x00000006, 0x00001556, 0x000200F8, 0x00001556,
    0x0007004F, 0x000003CC, 0x00001558, 0x000014BD, 0x000014BD, 0x00000000,
    0x00000001, 0x0006000C, 0x00000006, 0x00001559, 0x00000001, 0x0000003A,
    0x00001558, 0x000200F9, 0x0000155A, 0x000200F8, 0x0000154D, 0x00050051,
    0x0000000D, 0x0000154F, 0x000014BD, 0x00000000, 0x0007000C, 0x0000000D,
    0x00001657, 0x00000001, 0x00000028, 0x0000154F, 0x0000034F, 0x0007000C,
    0x0000000D, 0x00001658, 0x00000001, 0x00000025, 0x00001657, 0x000000CF,
    0x000500BE, 0x0000004D, 0x0000165A, 0x00001658, 0x000000CE, 0x000600A9,
    0x0000000D, 0x0000165B, 0x0000165A, 0x0000010B, 0x00000355, 0x0008000C,
    0x0000000D, 0x0000165F, 0x00000001, 0x00000032, 0x00001658, 0x00000358,
    0x0000165B, 0x0004006E, 0x0000002B, 0x00001660, 0x0000165F, 0x0004007C,
    0x00000006, 0x00001661, 0x00001660, 0x000500C7, 0x00000006, 0x00001662,
    0x00001661, 0x0000035E, 0x00050051, 0x0000000D, 0x00001552, 0x000014BD,
    0x00000001, 0x0007000C, 0x0000000D, 0x00001668, 0x00000001, 0x00000028,
    0x00001552, 0x0000034F, 0x0007000C, 0x0000000D, 0x00001669, 0x00000001,
    0x00000025, 0x00001668, 0x000000CF, 0x000500BE, 0x0000004D, 0x0000166B,
    0x00001669, 0x000000CE, 0x000600A9, 0x0000000D, 0x0000166C, 0x0000166B,
    0x0000010B, 0x00000355, 0x0008000C, 0x0000000D, 0x00001670, 0x00000001,
    0x00000032, 0x00001669, 0x00000358, 0x0000166C, 0x0004006E, 0x0000002B,
    0x00001671, 0x00001670, 0x0004007C, 0x00000006, 0x00001672, 0x00001671,
    0x000500C7, 0x00000006, 0x00001673, 0x00001672, 0x0000035E, 0x000500C4,
    0x00000006, 0x00001554, 0x00001673, 0x000000FB, 0x000500C5, 0x00000006,
    0x00001555, 0x00001662, 0x00001554, 0x000200F9, 0x0000155A, 0x000200F8,
    0x0000153A, 0x00050051, 0x0000000D, 0x0000153C, 0x000014BD, 0x00000000,
    0x0007000C, 0x0000000D, 0x000015BF, 0x00000001, 0x00000028, 0x0000153C,
    0x000000CE, 0x0007000C, 0x0000000D, 0x000015C0, 0x00000001, 0x00000025,
    0x000015BF, 0x00000392, 0x0004007C, 0x00000006, 0x000015CC, 0x000015C0,
    0x000500B0, 0x0000004D, 0x000015CE, 0x000015CC, 0x00000366, 0x000300F7,
    0x000015DE, 0x00000000, 0x000400FA, 0x000015CE, 0x000015CF, 0x000015DB,
    0x000200F8, 0x000015DB, 0x00050080, 0x00000006, 0x000015DD, 0x000015CC,
    0x0000037F, 0x000200F9, 0x000015DE, 0x000200F8, 0x000015CF, 0x000500C2,
    0x00000006, 0x000015D1, 0x000015CC, 0x0000036C, 0x00050082, 0x00000006,
    0x000015D3, 0x0000036F, 0x000015D1, 0x0007000C, 0x00000006, 0x000015D4,
    0x00000001, 0x00000026, 0x000015D3, 0x0000022A, 0x000500C7, 0x00000006,
    0x000015D6, 0x000015CC, 0x00000375, 0x000500C5, 0x00000006, 0x000015D7,
    0x000015D6, 0x00000377, 0x000500C2, 0x00000006, 0x000015DA, 0x000015D7,
    0x000015D4, 0x000200F9, 0x000015DE, 0x000200F8, 0x000015DE, 0x000700F5,
    0x00000006, 0x00001CC3, 0x000015DA, 0x000015CF, 0x000015DD, 0x000015DB,
    0x000500C2, 0x00000006, 0x000015E0, 0x00001CC3, 0x000000FB, 0x000500C7,
    0x00000006, 0x000015E1, 0x000015E0, 0x000000DC, 0x00050080, 0x00000006,
    0x000015E3, 0x00001CC3, 0x00000387, 0x00050080, 0x00000006, 0x000015E5,
    0x000015E3, 0x000015E1, 0x000500C2, 0x00000006, 0x000015E7, 0x000015E5,
    0x000000FB, 0x000500C7, 0x00000006, 0x000015E8, 0x000015E7, 0x00000215,
    0x00050051, 0x0000000D, 0x0000153F, 0x000014BD, 0x00000001, 0x0007000C,
    0x0000000D, 0x000015ED, 0x00000001, 0x00000028, 0x0000153F, 0x000000CE,
    0x0007000C, 0x0000000D, 0x000015EE, 0x00000001, 0x00000025, 0x000015ED,
    0x00000392, 0x0004007C, 0x00000006, 0x000015FA, 0x000015EE, 0x000500B0,
    0x0000004D, 0x000015FC, 0x000015FA, 0x00000366, 0x000300F7, 0x0000160C,
    0x00000000, 0x000400FA, 0x000015FC, 0x000015FD, 0x00001609, 0x000200F8,
    0x00001609, 0x00050080, 0x00000006, 0x0000160B, 0x000015FA, 0x0000037F,
    0x000200F9, 0x0000160C, 0x000200F8, 0x000015FD, 0x000500C2, 0x00000006,
    0x000015FF, 0x000015FA, 0x0000036C, 0x00050082, 0x00000006, 0x00001601,
    0x0000036F, 0x000015FF, 0x0007000C, 0x00000006, 0x00001602, 0x00000001,
    0x00000026, 0x00001601, 0x0000022A, 0x000500C7, 0x00000006, 0x00001604,
    0x000015FA, 0x00000375, 0x000500C5, 0x00000006, 0x00001605, 0x00001604,
    0x00000377, 0x000500C2, 0x00000006, 0x00001608, 0x00001605, 0x00001602,
    0x000200F9, 0x0000160C, 0x000200F8, 0x0000160C, 0x000700F5, 0x00000006,
    0x00001CC4, 0x00001608, 0x000015FD, 0x0000160B, 0x00001609, 0x000500C2,
    0x00000006, 0x0000160E, 0x00001CC4, 0x000000FB, 0x000500C7, 0x00000006,
    0x0000160F, 0x0000160E, 0x000000DC, 0x00050080, 0x00000006, 0x00001611,
    0x00001CC4, 0x00000387, 0x00050080, 0x00000006, 0x00001613, 0x00001611,
    0x0000160F, 0x000500C2, 0x00000006, 0x00001615, 0x00001613, 0x000000FB,
    0x000500C7, 0x00000006, 0x00001616, 0x00001615, 0x00000215, 0x000500C4,
    0x00000006, 0x00001541, 0x00001616, 0x00000219, 0x000500C5, 0x00000006,
    0x00001542, 0x000015E8, 0x00001541, 0x00050051, 0x0000000D, 0x00001544,
    0x000014BD, 0x00000002, 0x0007000C, 0x0000000D, 0x0000161B, 0x00000001,
    0x00000028, 0x00001544, 0x000000CE, 0x0007000C, 0x0000000D, 0x0000161C,
    0x00000001, 0x00000025, 0x0000161B, 0x00000392, 0x0004007C, 0x00000006,
    0x00001628, 0x0000161C, 0x000500B0, 0x0000004D, 0x0000162A, 0x00001628,
    0x00000366, 0x000300F7, 0x0000163A, 0x00000000, 0x000400FA, 0x0000162A,
    0x0000162B, 0x00001637, 0x000200F8, 0x00001637, 0x00050080, 0x00000006,
    0x00001639, 0x00001628, 0x0000037F, 0x000200F9, 0x0000163A, 0x000200F8,
    0x0000162B, 0x000500C2, 0x00000006, 0x0000162D, 0x00001628, 0x0000036C,
    0x00050082, 0x00000006, 0x0000162F, 0x0000036F, 0x0000162D, 0x0007000C,
    0x00000006, 0x00001630, 0x00000001, 0x00000026, 0x0000162F, 0x0000022A,
    0x000500C7, 0x00000006, 0x00001632, 0x00001628, 0x00000375, 0x000500C5,
    0x00000006, 0x00001633, 0x00001632, 0x00000377, 0x000500C2, 0x00000006,
    0x00001636, 0x00001633, 0x00001630, 0x000200F9, 0x0000163A, 0x000200F8,
    0x0000163A, 0x000700F5, 0x00000006, 0x00001CC5, 0x00001636, 0x0000162B,
    0x00001639, 0x00001637, 0x000500C2, 0x00000006, 0x0000163C, 0x00001CC5,
    0x000000FB, 0x000500C7, 0x00000006, 0x0000163D, 0x0000163C, 0x000000DC,
    0x00050080, 0x00000006, 0x0000163F, 0x00001CC5, 0x00000387, 0x00050080,
    0x00000006, 0x00001641, 0x0000163F, 0x0000163D, 0x000500C2, 0x00000006,
    0x00001643, 0x00001641, 0x000000FB, 0x000500C7, 0x00000006, 0x00001644,
    0x00001643, 0x00000215, 0x000500C4, 0x00000006, 0x00001546, 0x00001644,
    0x0000029C, 0x000500C5, 0x00000006, 0x00001547, 0x00001542, 0x00001546,
    0x00050051, 0x0000000D, 0x00001549, 0x000014BD, 0x00000003, 0x0008000C,
    0x0000000D, 0x00001651, 0x00000001, 0x0000002B, 0x00001549, 0x000000CE,
    0x000000CF, 0x0008000C, 0x0000000D, 0x0000164C, 0x00000001, 0x00000032,
    0x00001651, 0x00000128, 0x0000010B, 0x0004006D, 0x00000006, 0x0000164D,
    0x0000164C, 0x000500C4, 0x00000006, 0x0000154B, 0x0000164D, 0x000003BD,
    0x000500C5, 0x00000006, 0x0000154C, 0x00001547, 0x0000154B, 0x000200F9,
    0x0000155A, 0x000200F8, 0x00001537, 0x0008000C, 0x0000000F, 0x000015AC,
    0x00000001, 0x0000002B, 0x000014BD, 0x00001D37, 0x00001D38, 0x0008000C,
    0x0000000F, 0x00001595, 0x00000001, 0x00000032, 0x000015AC, 0x00000129,
    0x00001D39, 0x0004006D, 0x0000001D, 0x00001596, 0x00001595, 0x00050051,
    0x00000006, 0x00001598, 0x00001596, 0x00000000, 0x00050051, 0x00000006,
    0x0000159A, 0x00001596, 0x00000001, 0x000500C4, 0x00000006, 0x0000159B,
    0x0000159A, 0x00000132, 0x000500C5, 0x00000006, 0x0000159C, 0x00001598,
    0x0000159B, 0x00050051, 0x00000006, 0x0000159E, 0x00001596, 0x00000002,
    0x000500C4, 0x00000006, 0x0000159F, 0x0000159E, 0x00000137, 0x000500C5,
    0x00000006, 0x000015A0, 0x0000159C, 0x0000159F, 0x00050051, 0x00000006,
    0x000015A2, 0x00001596, 0x00000003, 0x000500C4, 0x00000006, 0x000015A3,
    0x000015A2, 0x0000013C, 0x000500C5, 0x00000006, 0x000015A4, 0x000015A0,
    0x000015A3, 0x000200F9, 0x0000155A, 0x000200F8, 0x00001534, 0x0008000C,
    0x0000000F, 0x0000157E, 0x00000001, 0x0000002B, 0x000014BD, 0x00001D37,
    0x00001D38, 0x0005008E, 0x0000000F, 0x00001565, 0x0000157E, 0x00000109,
    0x00050081, 0x0000000F, 0x00001567, 0x00001565, 0x00001D39, 0x0004006D,
    0x0000001D, 0x00001568, 0x00001567, 0x00050051, 0x00000006, 0x0000156A,
    0x00001568, 0x00000000, 0x00050051, 0x00000006, 0x0000156C, 0x00001568,
    0x00000001, 0x000500C4, 0x00000006, 0x0000156D, 0x0000156C, 0x00000114,
    0x000500C5, 0x00000006, 0x0000156E, 0x0000156A, 0x0000156D, 0x00050051,
    0x00000006, 0x00001570, 0x00001568, 0x00000002, 0x000500C4, 0x00000006,
    0x00001571, 0x00001570, 0x00000119, 0x000500C5, 0x00000006, 0x00001572,
    0x0000156E, 0x00001571, 0x00050051, 0x00000006, 0x00001574, 0x00001568,
    0x00000003, 0x000500C4, 0x00000006, 0x00001575, 0x00001574, 0x0000011E,
    0x000500C5, 0x00000006, 0x00001576, 0x00001572, 0x00001575, 0x000200F9,
    0x0000155A, 0x000200F8, 0x00001530, 0x00050051, 0x0000000D, 0x00001532,
    0x000014BD, 0x00000000, 0x0004007C, 0x00000006, 0x00001533, 0x00001532,
    0x000200F9, 0x0000155A, 0x000200F8, 0x0000155A, 0x000F00F5, 0x00000006,
    0x00001CC8, 0x00001533, 0x00001530, 0x00001576, 0x00001534, 0x000015A4,
    0x00001537, 0x0000154C, 0x0000163A, 0x00001555, 0x0000154D, 0x00001559,
    0x00001556, 0x000300F7, 0x0000168F, 0x00000000, 0x000400FA, 0x00000808,
    0x00001687, 0x00001689, 0x000200F8, 0x00001689, 0x000500AA, 0x0000004D,
    0x0000168B, 0x0000073D, 0x000001C4, 0x000600A9, 0x00000006, 0x00001D4C,
    0x0000168B, 0x000000DF, 0x0000010F, 0x000200F9, 0x0000168F, 0x000200F8,
    0x00001687, 0x000200F9, 0x0000168F, 0x000200F8, 0x0000168F, 0x000700F5,
    0x00000006, 0x00001CD4, 0x0000073D, 0x00001687, 0x00001D4C, 0x00001689,
    0x00050080, 0x00000006, 0x00001698, 0x00000818, 0x0000023A, 0x00050050,
    0x00000008, 0x0000169E, 0x00001698, 0x0000081F, 0x00050080, 0x00000008,
    0x000016A1, 0x0000169E, 0x000006FF, 0x000500C4, 0x00000008, 0x000016A3,
    0x000016A1, 0x000004F0, 0x00050050, 0x00000008, 0x000016B3, 0x00001CD4,
    0x00001CD4, 0x000500C2, 0x00000008, 0x000016AC, 0x000016B3, 0x00000429,
    0x000500C7, 0x00000008, 0x000016AE, 0x000016AC, 0x00001D33, 0x00050080,
    0x00000008, 0x000016A6, 0x000016A3, 0x000016AE, 0x00050051, 0x00000006,
    0x000016E5, 0x000016A6, 0x00000000, 0x00050086, 0x00000006, 0x000016E7,
    0x000016E5, 0x000008A1, 0x00050051, 0x00000006, 0x000016E9, 0x000016A6,
    0x00000001, 0x00050086, 0x00000006, 0x000016EB, 0x000016E9, 0x000008A6,
    0x00050084, 0x00000006, 0x000016F0, 0x000016E7, 0x000008A1, 0x00050082,
    0x00000006, 0x000016F1, 0x000016E5, 0x000016F0, 0x00050084, 0x00000006,
    0x000016F6, 0x000016EB, 0x000008A6, 0x00050082, 0x00000006, 0x000016F7,
    0x000016E9, 0x000016F6, 0x00050084, 0x00000006, 0x000016FB, 0x000016EB,
    0x0000087C, 0x00050080, 0x00000006, 0x000016FD, 0x000016FB, 0x000016E7,
    0x00050080, 0x00000006, 0x00001701, 0x00000881, 0x000016FD, 0x00050082,
    0x00000006, 0x00001705, 0x00001701, 0x00000886, 0x00050086, 0x00000006,
    0x0000170A, 0x00001705, 0x00000889, 0x00050084, 0x00000006, 0x0000170E,
    0x0000170A, 0x00000889, 0x00050082, 0x00000006, 0x0000170F, 0x00001705,
    0x0000170E, 0x00050084, 0x00000006, 0x00001712, 0x0000170F, 0x000008A1,
    0x00050080, 0x00000006, 0x00001714, 0x00001712, 0x000016F1, 0x00050084,
    0x00000006, 0x00001717, 0x0000170A, 0x000008A6, 0x00050080, 0x00000006,
    0x00001719, 0x00001717, 0x000016F7, 0x000500C7, 0x00000006, 0x00001729,
    0x00001719, 0x000000DC, 0x000500AB, 0x0000004D, 0x0000172A, 0x00001729,
    0x0000010F, 0x000300F7, 0x00001731, 0x00000000, 0x000400FA, 0x0000172A,
    0x0000172B, 0x0000172E, 0x000200F8, 0x0000172E, 0x00050041, 0x000003FC,
    0x0000172F, 0x000003FB, 0x00000114, 0x0004003D, 0x00000006, 0x00001730,
    0x0000172F, 0x000200F9, 0x00001731, 0x000200F8, 0x0000172B, 0x00050041,
    0x000003FC, 0x0000172C, 0x000003FB, 0x0000025F, 0x0004003D, 0x00000006,
    0x0000172D, 0x0000172C, 0x000200F9, 0x00001731, 0x000200F8, 0x00001731,
    0x000700F5, 0x00000006, 0x00001CD6, 0x0000172D, 0x0000172B, 0x00001730,
    0x0000172E, 0x0004007C, 0x0000002B, 0x000016C4, 0x00001714, 0x000500C2,
    0x00000006, 0x000016C7, 0x00001719, 0x000000DC, 0x0004007C, 0x0000002B,
    0x000016C8, 0x000016C7, 0x00050050, 0x00000033, 0x000016CC, 0x000016C4,
    0x000016C8, 0x0004007C, 0x0000002B, 0x000016CE, 0x00001CD6, 0x0007005F,
    0x0000000F, 0x000016CF, 0x00000843, 0x000016CC, 0x00000040, 0x000016CE,
    0x000300F7, 0x0000176C, 0x00000000, 0x001300FB, 0x000006DA, 0x00001742,
    0x00000000, 0x00001746, 0x00000001, 0x00001746, 0x00000002, 0x00001749,
    0x0000000A, 0x00001749, 0x00000003, 0x0000174C, 0x0000000C, 0x0000174C,
    0x00000004, 0x0000175F, 0x00000006, 0x00001768, 0x000200F8, 0x00001768,
    0x0007004F, 0x000003CC, 0x0000176A, 0x000016CF, 0x000016CF, 0x00000000,
    0x00000001, 0x0006000C, 0x00000006, 0x0000176B, 0x00000001, 0x0000003A,
    0x0000176A, 0x000200F9, 0x0000176C, 0x000200F8, 0x0000175F, 0x00050051,
    0x0000000D, 0x00001761, 0x000016CF, 0x00000000, 0x0007000C, 0x0000000D,
    0x00001869, 0x00000001, 0x00000028, 0x00001761, 0x0000034F, 0x0007000C,
    0x0000000D, 0x0000186A, 0x00000001, 0x00000025, 0x00001869, 0x000000CF,
    0x000500BE, 0x0000004D, 0x0000186C, 0x0000186A, 0x000000CE, 0x000600A9,
    0x0000000D, 0x0000186D, 0x0000186C, 0x0000010B, 0x00000355, 0x0008000C,
    0x0000000D, 0x00001871, 0x00000001, 0x00000032, 0x0000186A, 0x00000358,
    0x0000186D, 0x0004006E, 0x0000002B, 0x00001872, 0x00001871, 0x0004007C,
    0x00000006, 0x00001873, 0x00001872, 0x000500C7, 0x00000006, 0x00001874,
    0x00001873, 0x0000035E, 0x00050051, 0x0000000D, 0x00001764, 0x000016CF,
    0x00000001, 0x0007000C, 0x0000000D, 0x0000187A, 0x00000001, 0x00000028,
    0x00001764, 0x0000034F, 0x0007000C, 0x0000000D, 0x0000187B, 0x00000001,
    0x00000025, 0x0000187A, 0x000000CF, 0x000500BE, 0x0000004D, 0x0000187D,
    0x0000187B, 0x000000CE, 0x000600A9, 0x0000000D, 0x0000187E, 0x0000187D,
    0x0000010B, 0x00000355, 0x0008000C, 0x0000000D, 0x00001882, 0x00000001,
    0x00000032, 0x0000187B, 0x00000358, 0x0000187E, 0x0004006E, 0x0000002B,
    0x00001883, 0x00001882, 0x0004007C, 0x00000006, 0x00001884, 0x00001883,
    0x000500C7, 0x00000006, 0x00001885, 0x00001884, 0x0000035E, 0x000500C4,
    0x00000006, 0x00001766, 0x00001885, 0x000000FB, 0x000500C5, 0x00000006,
    0x00001767, 0x00001874, 0x00001766, 0x000200F9, 0x0000176C, 0x000200F8,
    0x0000174C, 0x00050051, 0x0000000D, 0x0000174E, 0x000016CF, 0x00000000,
    0x0007000C, 0x0000000D, 0x000017D1, 0x00000001, 0x00000028, 0x0000174E,
    0x000000CE, 0x0007000C, 0x0000000D, 0x000017D2, 0x00000001, 0x00000025,
    0x000017D1, 0x00000392, 0x0004007C, 0x00000006, 0x000017DE, 0x000017D2,
    0x000500B0, 0x0000004D, 0x000017E0, 0x000017DE, 0x00000366, 0x000300F7,
    0x000017F0, 0x00000000, 0x000400FA, 0x000017E0, 0x000017E1, 0x000017ED,
    0x000200F8, 0x000017ED, 0x00050080, 0x00000006, 0x000017EF, 0x000017DE,
    0x0000037F, 0x000200F9, 0x000017F0, 0x000200F8, 0x000017E1, 0x000500C2,
    0x00000006, 0x000017E3, 0x000017DE, 0x0000036C, 0x00050082, 0x00000006,
    0x000017E5, 0x0000036F, 0x000017E3, 0x0007000C, 0x00000006, 0x000017E6,
    0x00000001, 0x00000026, 0x000017E5, 0x0000022A, 0x000500C7, 0x00000006,
    0x000017E8, 0x000017DE, 0x00000375, 0x000500C5, 0x00000006, 0x000017E9,
    0x000017E8, 0x00000377, 0x000500C2, 0x00000006, 0x000017EC, 0x000017E9,
    0x000017E6, 0x000200F9, 0x000017F0, 0x000200F8, 0x000017F0, 0x000700F5,
    0x00000006, 0x00001CD7, 0x000017EC, 0x000017E1, 0x000017EF, 0x000017ED,
    0x000500C2, 0x00000006, 0x000017F2, 0x00001CD7, 0x000000FB, 0x000500C7,
    0x00000006, 0x000017F3, 0x000017F2, 0x000000DC, 0x00050080, 0x00000006,
    0x000017F5, 0x00001CD7, 0x00000387, 0x00050080, 0x00000006, 0x000017F7,
    0x000017F5, 0x000017F3, 0x000500C2, 0x00000006, 0x000017F9, 0x000017F7,
    0x000000FB, 0x000500C7, 0x00000006, 0x000017FA, 0x000017F9, 0x00000215,
    0x00050051, 0x0000000D, 0x00001751, 0x000016CF, 0x00000001, 0x0007000C,
    0x0000000D, 0x000017FF, 0x00000001, 0x00000028, 0x00001751, 0x000000CE,
    0x0007000C, 0x0000000D, 0x00001800, 0x00000001, 0x00000025, 0x000017FF,
    0x00000392, 0x0004007C, 0x00000006, 0x0000180C, 0x00001800, 0x000500B0,
    0x0000004D, 0x0000180E, 0x0000180C, 0x00000366, 0x000300F7, 0x0000181E,
    0x00000000, 0x000400FA, 0x0000180E, 0x0000180F, 0x0000181B, 0x000200F8,
    0x0000181B, 0x00050080, 0x00000006, 0x0000181D, 0x0000180C, 0x0000037F,
    0x000200F9, 0x0000181E, 0x000200F8, 0x0000180F, 0x000500C2, 0x00000006,
    0x00001811, 0x0000180C, 0x0000036C, 0x00050082, 0x00000006, 0x00001813,
    0x0000036F, 0x00001811, 0x0007000C, 0x00000006, 0x00001814, 0x00000001,
    0x00000026, 0x00001813, 0x0000022A, 0x000500C7, 0x00000006, 0x00001816,
    0x0000180C, 0x00000375, 0x000500C5, 0x00000006, 0x00001817, 0x00001816,
    0x00000377, 0x000500C2, 0x00000006, 0x0000181A, 0x00001817, 0x00001814,
    0x000200F9, 0x0000181E, 0x000200F8, 0x0000181E, 0x000700F5, 0x00000006,
    0x00001CD8, 0x0000181A, 0x0000180F, 0x0000181D, 0x0000181B, 0x000500C2,
    0x00000006, 0x00001820, 0x00001CD8, 0x000000FB, 0x000500C7, 0x00000006,
    0x00001821, 0x00001820, 0x000000DC, 0x00050080, 0x00000006, 0x00001823,
    0x00001CD8, 0x00000387, 0x00050080, 0x00000006, 0x00001825, 0x00001823,
    0x00001821, 0x000500C2, 0x00000006, 0x00001827, 0x00001825, 0x000000FB,
    0x000500C7, 0x00000006, 0x00001828, 0x00001827, 0x00000215, 0x000500C4,
    0x00000006, 0x00001753, 0x00001828, 0x00000219, 0x000500C5, 0x00000006,
    0x00001754, 0x000017FA, 0x00001753, 0x00050051, 0x0000000D, 0x00001756,
    0x000016CF, 0x00000002, 0x0007000C, 0x0000000D, 0x0000182D, 0x00000001,
    0x00000028, 0x00001756, 0x000000CE, 0x0007000C, 0x0000000D, 0x0000182E,
    0x00000001, 0x00000025, 0x0000182D, 0x00000392, 0x0004007C, 0x00000006,
    0x0000183A, 0x0000182E, 0x000500B0, 0x0000004D, 0x0000183C, 0x0000183A,
    0x00000366, 0x000300F7, 0x0000184C, 0x00000000, 0x000400FA, 0x0000183C,
    0x0000183D, 0x00001849, 0x000200F8, 0x00001849, 0x00050080, 0x00000006,
    0x0000184B, 0x0000183A, 0x0000037F, 0x000200F9, 0x0000184C, 0x000200F8,
    0x0000183D, 0x000500C2, 0x00000006, 0x0000183F, 0x0000183A, 0x0000036C,
    0x00050082, 0x00000006, 0x00001841, 0x0000036F, 0x0000183F, 0x0007000C,
    0x00000006, 0x00001842, 0x00000001, 0x00000026, 0x00001841, 0x0000022A,
    0x000500C7, 0x00000006, 0x00001844, 0x0000183A, 0x00000375, 0x000500C5,
    0x00000006, 0x00001845, 0x00001844, 0x00000377, 0x000500C2, 0x00000006,
    0x00001848, 0x00001845, 0x00001842, 0x000200F9, 0x0000184C, 0x000200F8,
    0x0000184C, 0x000700F5, 0x00000006, 0x00001CD9, 0x00001848, 0x0000183D,
    0x0000184B, 0x00001849, 0x000500C2, 0x00000006, 0x0000184E, 0x00001CD9,
    0x000000FB, 0x000500C7, 0x00000006, 0x0000184F, 0x0000184E, 0x000000DC,
    0x00050080, 0x00000006, 0x00001851, 0x00001CD9, 0x00000387, 0x00050080,
    0x00000006, 0x00001853, 0x00001851, 0x0000184F, 0x000500C2, 0x00000006,
    0x00001855, 0x00001853, 0x000000FB, 0x000500C7, 0x00000006, 0x00001856,
    0x00001855, 0x00000215, 0x000500C4, 0x00000006, 0x00001758, 0x00001856,
    0x0000029C, 0x000500C5, 0x00000006, 0x00001759, 0x00001754, 0x00001758,
    0x00050051, 0x0000000D, 0x0000175B, 0x000016CF, 0x00000003, 0x0008000C,
    0x0000000D, 0x00001863, 0x00000001, 0x0000002B, 0x0000175B, 0x000000CE,
    0x000000CF, 0x0008000C, 0x0000000D, 0x0000185E, 0x00000001, 0x00000032,
    0x00001863, 0x00000128, 0x0000010B, 0x0004006D, 0x00000006, 0x0000185F,
    0x0000185E, 0x000500C4, 0x00000006, 0x0000175D, 0x0000185F, 0x000003BD,
    0x000500C5, 0x00000006, 0x0000175E, 0x00001759, 0x0000175D, 0x000200F9,
    0x0000176C, 0x000200F8, 0x00001749, 0x0008000C, 0x0000000F, 0x000017BE,
    0x00000001, 0x0000002B, 0x000016CF, 0x00001D37, 0x00001D38, 0x0008000C,
    0x0000000F, 0x000017A7, 0x00000001, 0x00000032, 0x000017BE, 0x00000129,
    0x00001D39, 0x0004006D, 0x0000001D, 0x000017A8, 0x000017A7, 0x00050051,
    0x00000006, 0x000017AA, 0x000017A8, 0x00000000, 0x00050051, 0x00000006,
    0x000017AC, 0x000017A8, 0x00000001, 0x000500C4, 0x00000006, 0x000017AD,
    0x000017AC, 0x00000132, 0x000500C5, 0x00000006, 0x000017AE, 0x000017AA,
    0x000017AD, 0x00050051, 0x00000006, 0x000017B0, 0x000017A8, 0x00000002,
    0x000500C4, 0x00000006, 0x000017B1, 0x000017B0, 0x00000137, 0x000500C5,
    0x00000006, 0x000017B2, 0x000017AE, 0x000017B1, 0x00050051, 0x00000006,
    0x000017B4, 0x000017A8, 0x00000003, 0x000500C4, 0x00000006, 0x000017B5,
    0x000017B4, 0x0000013C, 0x000500C5, 0x00000006, 0x000017B6, 0x000017B2,
    0x000017B5, 0x000200F9, 0x0000176C, 0x000200F8, 0x00001746, 0x0008000C,
    0x0000000F, 0x00001790, 0x00000001, 0x0000002B, 0x000016CF, 0x00001D37,
    0x00001D38, 0x0005008E, 0x0000000F, 0x00001777, 0x00001790, 0x00000109,
    0x00050081, 0x0000000F, 0x00001779, 0x00001777, 0x00001D39, 0x0004006D,
    0x0000001D, 0x0000177A, 0x00001779, 0x00050051, 0x00000006, 0x0000177C,
    0x0000177A, 0x00000000, 0x00050051, 0x00000006, 0x0000177E, 0x0000177A,
    0x00000001, 0x000500C4, 0x00000006, 0x0000177F, 0x0000177E, 0x00000114,
    0x000500C5, 0x00000006, 0x00001780, 0x0000177C, 0x0000177F, 0x00050051,
    0x00000006, 0x00001782, 0x0000177A, 0x00000002, 0x000500C4, 0x00000006,
    0x00001783, 0x00001782, 0x00000119, 0x000500C5, 0x00000006, 0x00001784,
    0x00001780, 0x00001783, 0x00050051, 0x00000006, 0x00001786, 0x0000177A,
    0x00000003, 0x000500C4, 0x00000006, 0x00001787, 0x00001786, 0x0000011E,
    0x000500C5, 0x00000006, 0x00001788, 0x00001784, 0x00001787, 0x000200F9,
    0x0000176C, 0x000200F8, 0x00001742, 0x00050051, 0x0000000D, 0x00001744,
    0x000016CF, 0x00000000, 0x0004007C, 0x00000006, 0x00001745, 0x00001744,
    0x000200F9, 0x0000176C, 0x000200F8, 0x0000176C, 0x000F00F5, 0x00000006,
    0x00001CDC, 0x00001745, 0x00001742, 0x00001788, 0x00001746, 0x000017B6,
    0x00001749, 0x0000175E, 0x0000184C, 0x00001767, 0x0000175F, 0x0000176B,
    0x00001768, 0x00070050, 0x0000001D, 0x00001D44, 0x00001C6F, 0x00001CB4,
    0x00001CC8, 0x00001CDC, 0x000500AA, 0x0000004D, 0x00001889, 0x00000818,
    0x0000010F, 0x000300F7, 0x0000188E, 0x00000000, 0x000400FA, 0x00001889,
    0x0000188A, 0x0000188E, 0x000200F8, 0x0000188A, 0x00050051, 0x00000006,
    0x0000188C, 0x00001C09, 0x00000000, 0x000500AB, 0x0000004D, 0x0000188D,
    0x0000188C, 0x0000010F, 0x000200F9, 0x0000188E, 0x000200F8, 0x0000188E,
    0x000700F5, 0x0000004D, 0x0000188F, 0x00001889, 0x0000176C, 0x0000188D,
    0x0000188A, 0x000300F7, 0x000018A4, 0x00000002, 0x000400FA, 0x0000188F,
    0x00001890, 0x000018A4, 0x000200F8, 0x00001890, 0x00050051, 0x00000006,
    0x00001892, 0x00001C09, 0x00000000, 0x000500AE, 0x0000004D, 0x00001893,
    0x00001892, 0x000000DF, 0x000300F7, 0x000018A0, 0x00000000, 0x000400FA,
    0x00001893, 0x00001894, 0x000018A0, 0x000200F8, 0x00001894, 0x000500AE,
    0x0000004D, 0x00001897, 0x00001892, 0x000000F5, 0x000300F7, 0x0000189C,
    0x00000000, 0x000400FA, 0x00001897, 0x00001898, 0x0000189C, 0x000200F8,
    0x00001898, 0x00060052, 0x0000001D, 0x00001BFC, 0x00001C5B, 0x00001D43,
    0x00000002, 0x000200F9, 0x0000189C, 0x000200F8, 0x0000189C, 0x000700F5,
    0x0000001D, 0x00001D15, 0x00001D43, 0x00001894, 0x00001BFC, 0x00001898,
    0x00050051, 0x00000006, 0x0000189E, 0x00001D15, 0x00000002, 0x00060052,
    0x0000001D, 0x00001BFF, 0x0000189E, 0x00001D15, 0x00000001, 0x000200F9,
    0x000018A0, 0x000200F8, 0x000018A0, 0x000700F5, 0x0000001D, 0x00001D16,
    0x00001D43, 0x00001890, 0x00001BFF, 0x0000189C, 0x00050051, 0x00000006,
    0x000018A2, 0x00001D16, 0x00000001, 0x00060052, 0x0000001D, 0x00001C02,
    0x000018A2, 0x00001D16, 0x00000000, 0x000200F9, 0x000018A4, 0x000200F8,
    0x000018A4, 0x000700F5, 0x0000001D, 0x00001D17, 0x00001D43, 0x0000188E,
    0x00001C02, 0x000018A0, 0x000300F7, 0x000018BA, 0x00000002, 0x000400FA,
    0x00000726, 0x000018AC, 0x000018BA, 0x000200F8, 0x000018AC, 0x000300F7,
    0x000018B9, 0x00000000, 0x000F00FB, 0x000006DA, 0x000018B9, 0x00000000,
    0x000018AF, 0x00000001, 0x000018AF, 0x00000002, 0x000018B4, 0x00000003,
    0x000018B4, 0x0000000A, 0x000018B4, 0x0000000C, 0x000018B4, 0x000200F8,
    0x000018B4, 0x000500C7, 0x0000001D, 0x000018E1, 0x00001D17, 0x00001D3D,
    0x000500C7, 0x0000001D, 0x000018E4, 0x00001D17, 0x00001D3E, 0x000500C4,
    0x0000001D, 0x000018E6, 0x000018E4, 0x00001D3F, 0x000500C5, 0x0000001D,
    0x000018E7, 0x000018E1, 0x000018E6, 0x000500C2, 0x0000001D, 0x000018EA,
    0x00001D17, 0x00001D3F, 0x000500C7, 0x0000001D, 0x000018EC, 0x000018EA,
    0x00001D3E, 0x000500C5, 0x0000001D, 0x000018ED, 0x000018E7, 0x000018EC,
    0x000500C7, 0x0000001D, 0x000018F2, 0x00001D44, 0x00001D3D, 0x000500C7,
    0x0000001D, 0x000018F5, 0x00001D44, 0x00001D3E, 0x000500C4, 0x0000001D,
    0x000018F7, 0x000018F5, 0x00001D3F, 0x000500C5, 0x0000001D, 0x000018F8,
    0x000018F2, 0x000018F7, 0x000500C2, 0x0000001D, 0x000018FB, 0x00001D44,
    0x00001D3F, 0x000500C7, 0x0000001D, 0x000018FD, 0x000018FB, 0x00001D3E,
    0x000500C5, 0x0000001D, 0x000018FE, 0x000018F8, 0x000018FD, 0x000200F9,
    0x000018B9, 0x000200F8, 0x000018AF, 0x000500C7, 0x0000001D, 0x000018BF,
    0x00001D17, 0x00001D3A, 0x000500C7, 0x0000001D, 0x000018C2, 0x00001D17,
    0x00001D3B, 0x000500C4, 0x0000001D, 0x000018C4, 0x000018C2, 0x00001D3C,
    0x000500C5, 0x0000001D, 0x000018C5, 0x000018BF, 0x000018C4, 0x000500C2,
    0x0000001D, 0x000018C8, 0x00001D17, 0x00001D3C, 0x000500C7, 0x0000001D,
    0x000018CA, 0x000018C8, 0x00001D3B, 0x000500C5, 0x0000001D, 0x000018CB,
    0x000018C5, 0x000018CA, 0x000500C7, 0x0000001D, 0x000018D0, 0x00001D44,
    0x00001D3A, 0x000500C7, 0x0000001D, 0x000018D3, 0x00001D44, 0x00001D3B,
    0x000500C4, 0x0000001D, 0x000018D5, 0x000018D3, 0x00001D3C, 0x000500C5,
    0x0000001D, 0x000018D6, 0x000018D0, 0x000018D5, 0x000500C2, 0x0000001D,
    0x000018D9, 0x00001D44, 0x00001D3C, 0x000500C7, 0x0000001D, 0x000018DB,
    0x000018D9, 0x00001D3B, 0x000500C5, 0x0000001D, 0x000018DC, 0x000018D6,
    0x000018DB, 0x000200F9, 0x000018B9, 0x000200F8, 0x000018B9, 0x000900F5,
    0x0000001D, 0x00001D23, 0x00001D44, 0x000018AC, 0x000018DC, 0x000018AF,
    0x000018FE, 0x000018B4, 0x000900F5, 0x0000001D, 0x00001D21, 0x00001D17,
    0x000018AC, 0x000018CB, 0x000018AF, 0x000018ED, 0x000018B4, 0x000200F9,
    0x000018BA, 0x000200F8, 0x000018BA, 0x000700F5, 0x0000001D, 0x00001D22,
    0x00001D44, 0x000018A4, 0x00001D23, 0x000018B9, 0x000700F5, 0x0000001D,
    0x00001D20, 0x00001D17, 0x000018A4, 0x00001D21, 0x000018B9, 0x00050080,
    0x00000008, 0x00001906, 0x00001C0B, 0x00000739, 0x000500C2, 0x00000008,
    0x00001939, 0x00001906, 0x00000254, 0x00050086, 0x00000008, 0x0000193B,
    0x00001939, 0x000006E4, 0x00050084, 0x00000008, 0x0000193E, 0x000006E4,
    0x0000193B, 0x00050082, 0x00000008, 0x0000193F, 0x00001939, 0x0000193E,
    0x000500C4, 0x00000008, 0x00001942, 0x0000193B, 0x00000254, 0x00050051,
    0x00000006, 0x00001945, 0x0000193F, 0x00000000, 0x00050084, 0x00000006,
    0x00001947, 0x00001945, 0x000008A5, 0x00050051, 0x00000006, 0x00001949,
    0x0000193F, 0x00000001, 0x00050080, 0x00000006, 0x0000194A, 0x00001947,
    0x00001949, 0x000500C7, 0x00000008, 0x00001953, 0x00001906, 0x000006F9,
    0x000500C4, 0x00000006, 0x00001959, 0x0000194A, 0x0000023A, 0x00050051,
    0x00000006, 0x0000195B, 0x00001953, 0x00000001, 0x000500C4, 0x00000006,
    0x0000195D, 0x0000195B, 0x00000594, 0x000500C5, 0x00000006, 0x0000195E,
    0x00001959, 0x0000195D, 0x00050051, 0x00000006, 0x00001960, 0x00001953,
    0x00000000, 0x000500C4, 0x00000006, 0x00001961, 0x00001960, 0x000000DF,
    0x000500C5, 0x00000006, 0x00001962, 0x0000195E, 0x00001961, 0x000300F7,
    0x0000191F, 0x00000002, 0x000400FA, 0x00000711, 0x0000190E, 0x00001919,
    0x000200F8, 0x00001919, 0x0004007C, 0x00000033, 0x0000191B, 0x00001942,
    0x00050051, 0x0000002B, 0x000019C6, 0x0000191B, 0x00000001, 0x000500C3,
    0x0000002B, 0x000019C7, 0x000019C6, 0x0000014F, 0x0004007C, 0x0000002B,
    0x000019C8, 0x00000729, 0x00050084, 0x0000002B, 0x000019C9, 0x000019C7,
    0x000019C8, 0x00050051, 0x0000002B, 0x000019CA, 0x0000191B, 0x00000000,
    0x000500C3, 0x0000002B, 0x000019CB, 0x000019CA, 0x0000014F, 0x00050080,
    0x0000002B, 0x000019CC, 0x000019C9, 0x000019CB, 0x000500C4, 0x0000002B,
    0x000019CD, 0x000019CC, 0x00000143, 0x000500C3, 0x0000002B, 0x000019CF,
    0x000019C6, 0x0000014D, 0x000500C7, 0x0000002B, 0x000019D0, 0x000019CF,
    0x00000153, 0x000500C4, 0x0000002B, 0x000019D1, 0x000019D0, 0x0000016B,
    0x000500C7, 0x0000002B, 0x000019D3, 0x000019CA, 0x00000153, 0x000500C5,
    0x0000002B, 0x000019D4, 0x000019D1, 0x000019D3, 0x000500C5, 0x0000002B,
    0x000019D7, 0x000019CD, 0x000019D4, 0x000500C4, 0x0000002B, 0x000019D8,
    0x000019D7, 0x000000DF, 0x000500C3, 0x0000002B, 0x000019DA, 0x000019C6,
    0x00000141, 0x000500C7, 0x0000002B, 0x000019DB, 0x000019DA, 0x0000014D,
    0x000500C3, 0x0000002B, 0x000019DD, 0x000019CA, 0x0000016B, 0x000500C7,
    0x0000002B, 0x000019DE, 0x000019DD, 0x0000016B, 0x000500C3, 0x0000002B,
    0x000019E0, 0x000019C6, 0x0000016B, 0x000500C7, 0x0000002B, 0x000019E1,
    0x000019E0, 0x0000014D, 0x000500C4, 0x0000002B, 0x000019E2, 0x000019E1,
    0x0000014D, 0x000500C6, 0x0000002B, 0x000019E3, 0x000019DE, 0x000019E2,
    0x000500C7, 0x0000002B, 0x000019E8, 0x000019C6, 0x0000014D, 0x000500C4,
    0x0000002B, 0x000019EC, 0x000019E8, 0x00000141, 0x000500C4, 0x0000002B,
    0x000019ED, 0x000019E3, 0x00000143, 0x000500C5, 0x0000002B, 0x000019EE,
    0x000019EC, 0x000019ED, 0x000500C4, 0x0000002B, 0x000019EF, 0x000019DB,
    0x00000146, 0x000500C5, 0x0000002B, 0x000019F0, 0x000019EE, 0x000019EF,
    0x000500C7, 0x0000002B, 0x000019F1, 0x000019D8, 0x00000149, 0x000500C5,
    0x0000002B, 0x000019F2, 0x000019F0, 0x000019F1, 0x000500C3, 0x0000002B,
    0x000019F3, 0x000019D8, 0x00000141, 0x000500C7, 0x0000002B, 0x000019F4,
    0x000019F3, 0x0000014D, 0x000500C4, 0x0000002B, 0x000019F5, 0x000019F4,
    0x0000014F, 0x000500C5, 0x0000002B, 0x000019F6, 0x000019F2, 0x000019F5,
    0x000500C3, 0x0000002B, 0x000019F7, 0x000019D8, 0x0000014F, 0x000500C7,
    0x0000002B, 0x000019F8, 0x000019F7, 0x00000153, 0x000500C4, 0x0000002B,
    0x000019F9, 0x000019F8, 0x00000114, 0x000500C5, 0x0000002B, 0x000019FA,
    0x000019F6, 0x000019F9, 0x000500C3, 0x0000002B, 0x000019FB, 0x000019D8,
    0x00000114, 0x000500C4, 0x0000002B, 0x000019FC, 0x000019FB, 0x00000158,
    0x000500C5, 0x0000002B, 0x000019FD, 0x000019FA, 0x000019FC, 0x0004007C,
    0x00000006, 0x0000191E, 0x000019FD, 0x000200F9, 0x0000191F, 0x000200F8,
    0x0000190E, 0x00050051, 0x00000006, 0x00001911, 0x00001942, 0x00000000,
    0x00050051, 0x00000006, 0x00001912, 0x00001942, 0x00000001, 0x00060050,
    0x000002C1, 0x00001913, 0x00001911, 0x00001912, 0x00000715, 0x0004007C,
    0x0000003A, 0x00001914, 0x00001913, 0x00050051, 0x0000002B, 0x0000197D,
    0x00001914, 0x00000002, 0x000500C3, 0x0000002B, 0x0000197E, 0x0000197D,
    0x0000018C, 0x0004007C, 0x0000002B, 0x0000197F, 0x0000072E, 0x00050084,
    0x0000002B, 0x00001980, 0x0000197E, 0x0000197F, 0x00050051, 0x0000002B,
    0x00001981, 0x00001914, 0x00000001, 0x000500C3, 0x0000002B, 0x00001982,
    0x00001981, 0x00000141, 0x00050080, 0x0000002B, 0x00001983, 0x00001980,
    0x00001982, 0x0004007C, 0x0000002B, 0x00001984, 0x00000729, 0x00050084,
    0x0000002B, 0x00001985, 0x00001983, 0x00001984, 0x00050051, 0x0000002B,
    0x00001986, 0x00001914, 0x00000000, 0x000500C3, 0x0000002B, 0x00001987,
    0x00001986, 0x0000014F, 0x00050080, 0x0000002B, 0x00001988, 0x00001985,
    0x00001987, 0x000500C4, 0x0000002B, 0x00001989, 0x00001988, 0x00000153,
    0x000500C7, 0x0000002B, 0x0000198B, 0x0000197D, 0x0000016B, 0x000500C4,
    0x0000002B, 0x0000198C, 0x0000198B, 0x0000014F, 0x000500C3, 0x0000002B,
    0x0000198E, 0x00001981, 0x0000014D, 0x000500C7, 0x0000002B, 0x0000198F,
    0x0000198E, 0x0000016B, 0x000500C4, 0x0000002B, 0x00001990, 0x0000198F,
    0x0000016B, 0x000500C5, 0x0000002B, 0x00001991, 0x0000198C, 0x00001990,
    0x000500C7, 0x0000002B, 0x00001993, 0x00001986, 0x00000153, 0x000500C5,
    0x0000002B, 0x00001994, 0x00001991, 0x00001993, 0x000500C5, 0x0000002B,
    0x00001997, 0x00001989, 0x00001994, 0x000500C4, 0x0000002B, 0x00001998,
    0x00001997, 0x000000DF, 0x000500C3, 0x0000002B, 0x0000199A, 0x00001981,
    0x0000016B, 0x000500C6, 0x0000002B, 0x0000199D, 0x0000199A, 0x0000197E,
    0x000500C7, 0x0000002B, 0x0000199E, 0x0000199D, 0x0000014D, 0x000500C3,
    0x0000002B, 0x000019A0, 0x00001986, 0x0000016B, 0x000500C7, 0x0000002B,
    0x000019A1, 0x000019A0, 0x0000016B, 0x000500C4, 0x0000002B, 0x000019A3,
    0x0000199E, 0x0000014D, 0x000500C6, 0x0000002B, 0x000019A4, 0x000019A1,
    0x000019A3, 0x000500C7, 0x0000002B, 0x000019A9, 0x00001981, 0x0000014D,
    0x000500C4, 0x0000002B, 0x000019AD, 0x000019A9, 0x00000141, 0x000500C4,
    0x0000002B, 0x000019AE, 0x000019A4, 0x00000143, 0x000500C5, 0x0000002B,
    0x000019AF, 0x000019AD, 0x000019AE, 0x000500C4, 0x0000002B, 0x000019B0,
    0x0000199E, 0x00000146, 0x000500C5, 0x0000002B, 0x000019B1, 0x000019AF,
    0x000019B0, 0x000500C7, 0x0000002B, 0x000019B2, 0x00001998, 0x00000149,
    0x000500C5, 0x0000002B, 0x000019B3, 0x000019B1, 0x000019B2, 0x000500C3,
    0x0000002B, 0x000019B4, 0x00001998, 0x00000141, 0x000500C7, 0x0000002B,
    0x000019B5, 0x000019B4, 0x0000014D, 0x000500C4, 0x0000002B, 0x000019B6,
    0x000019B5, 0x0000014F, 0x000500C5, 0x0000002B, 0x000019B7, 0x000019B3,
    0x000019B6, 0x000500C3, 0x0000002B, 0x000019B8, 0x00001998, 0x0000014F,
    0x000500C7, 0x0000002B, 0x000019B9, 0x000019B8, 0x00000153, 0x000500C4,
    0x0000002B, 0x000019BA, 0x000019B9, 0x00000114, 0x000500C5, 0x0000002B,
    0x000019BB, 0x000019B7, 0x000019BA, 0x000500C3, 0x0000002B, 0x000019BC,
    0x00001998, 0x00000114, 0x000500C4, 0x0000002B, 0x000019BD, 0x000019BC,
    0x00000158, 0x000500C5, 0x0000002B, 0x000019BE, 0x000019BB, 0x000019BD,
    0x0004007C, 0x00000006, 0x00001918, 0x000019BE, 0x000200F9, 0x0000191F,
    0x000200F8, 0x0000191F, 0x000700F5, 0x00000006, 0x00001D25, 0x00001918,
    0x0000190E, 0x0000191E, 0x00001919, 0x00050084, 0x00000006, 0x00001923,
    0x00000705, 0x000008A5, 0x00050084, 0x00000006, 0x00001924, 0x00001D25,
    0x00001923, 0x00050080, 0x00000006, 0x00001927, 0x00001924, 0x00001962,
    0x000500C2, 0x00000006, 0x000006A8, 0x00001927, 0x00000141, 0x000500AA,
    0x0000004D, 0x00001A01, 0x0000070D, 0x000000DC, 0x000500AA, 0x0000004D,
    0x00001A03, 0x0000070D, 0x000000DF, 0x000500A6, 0x0000004D, 0x00001A04,
    0x00001A01, 0x00001A03, 0x000300F7, 0x00001A11, 0x00000000, 0x000400FA,
    0x00001A04, 0x00001A05, 0x00001A11, 0x000200F8, 0x00001A05, 0x000500C7,
    0x0000001D, 0x00001A08, 0x00001D20, 0x00001D40, 0x000500C4, 0x0000001D,
    0x00001A0A, 0x00001A08, 0x00001D41, 0x000500C7, 0x0000001D, 0x00001A0D,
    0x00001D20, 0x00001D3A, 0x000500C2, 0x0000001D, 0x00001A0F, 0x00001A0D,
    0x00001D41, 0x000500C5, 0x0000001D, 0x00001A10, 0x00001A0A, 0x00001A0F,
    0x000200F9, 0x00001A11, 0x000200F8, 0x00001A11, 0x000700F5, 0x0000001D,
    0x00001D28, 0x00001D20, 0x0000191F, 0x00001A10, 0x00001A05, 0x000500AA,
    0x0000004D, 0x00001A15, 0x0000070D, 0x000000F5, 0x000500A6, 0x0000004D,
    0x00001A16, 0x00001A03, 0x00001A15, 0x000300F7, 0x00001A1F, 0x00000000,
    0x000400FA, 0x00001A16, 0x00001A17, 0x00001A1F, 0x000200F8, 0x00001A17,
    0x000500C4, 0x0000001D, 0x00001A1A, 0x00001D28, 0x00001D3C, 0x000500C2,
    0x0000001D, 0x00001A1D, 0x00001D28, 0x00001D3C, 0x000500C5, 0x0000001D,
    0x00001A1E, 0x00001A1A, 0x00001A1D, 0x000200F9, 0x00001A1F, 0x000200F8,
    0x00001A1F, 0x000700F5, 0x0000001D, 0x00001D29, 0x00001D28, 0x00001A11,
    0x00001A1E, 0x00001A17, 0x00060041, 0x000005D7, 0x000006AD, 0x000005CE,
    0x000001DD, 0x000006A8, 0x0003003E, 0x000006AD, 0x00001D29, 0x00050080,
    0x00000006, 0x000006B0, 0x00001927, 0x000000FB, 0x000500C2, 0x00000006,
    0x000006B2, 0x000006B0, 0x00000141, 0x000300F7, 0x00001A37, 0x00000000,
    0x000400FA, 0x00001A04, 0x00001A2B, 0x00001A37, 0x000200F8, 0x00001A2B,
    0x000500C7, 0x0000001D, 0x00001A2E, 0x00001D22, 0x00001D40, 0x000500C4,
    0x0000001D, 0x00001A30, 0x00001A2E, 0x00001D41, 0x000500C7, 0x0000001D,
    0x00001A33, 0x00001D22, 0x00001D3A, 0x000500C2, 0x0000001D, 0x00001A35,
    0x00001A33, 0x00001D41, 0x000500C5, 0x0000001D, 0x00001A36, 0x00001A30,
    0x00001A35, 0x000200F9, 0x00001A37, 0x000200F8, 0x00001A37, 0x000700F5,
    0x0000001D, 0x00001D30, 0x00001D22, 0x00001A1F, 0x00001A36, 0x00001A2B,
    0x000300F7, 0x00001A45, 0x00000000, 0x000400FA, 0x00001A16, 0x00001A3D,
    0x00001A45, 0x000200F8, 0x00001A3D, 0x000500C4, 0x0000001D, 0x00001A40,
    0x00001D30, 0x00001D3C, 0x000500C2, 0x0000001D, 0x00001A43, 0x00001D30,
    0x00001D3C, 0x000500C5, 0x0000001D, 0x00001A44, 0x00001A40, 0x00001A43,
    0x000200F9, 0x00001A45, 0x000200F8, 0x00001A45, 0x000700F5, 0x0000001D,
    0x00001D31, 0x00001D30, 0x00001A37, 0x00001A44, 0x00001A3D, 0x00060041,
    0x000005D7, 0x000006B7, 0x000005CE, 0x000001DD, 0x000006B2, 0x0003003E,
    0x000006B7, 0x00001D31, 0x000200F9, 0x000006B8, 0x000200F8, 0x000006B8,
    0x000100FD, 0x00010038,
};
