// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.3
; Generator: Google Shaderc over Glslang; 10
; Bound: 3726
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
     %v4uint = OpTypeVector %uint 4
        %int = OpTypeInt 32 1
      %v2int = OpTypeVector %int 2
      %v3int = OpTypeVector %int 3
       %bool = OpTypeBool
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
     %uint_1 = OpConstant %uint 1
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
     %uint_8 = OpConstant %uint 8
%uint_4278255360 = OpConstant %uint 4278255360
     %uint_3 = OpConstant %uint 3
    %uint_16 = OpConstant %uint 16
     %uint_4 = OpConstant %uint 4
      %int_4 = OpConstant %int 4
      %int_6 = OpConstant %int 6
     %int_11 = OpConstant %int 11
     %int_15 = OpConstant %int 15
      %int_1 = OpConstant %int 1
      %int_5 = OpConstant %int 5
      %int_7 = OpConstant %int 7
      %int_8 = OpConstant %int 8
     %int_12 = OpConstant %int 12
     %uint_0 = OpConstant %uint 0
      %int_3 = OpConstant %int 3
      %int_2 = OpConstant %int 2
     %uint_5 = OpConstant %uint 5
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
        %470 = OpConstantComposite %v2uint %uint_16 %uint_19
     %uint_7 = OpConstant %uint 7
%uint_536870912 = OpConstant %uint 536870912
        %494 = OpConstantComposite %v2uint %uint_0 %uint_4
        %498 = OpConstantComposite %v2uint %uint_4 %uint_1
      %int_9 = OpConstant %int 9
    %float_1 = OpConstant %float 1
%uint_16777216 = OpConstant %uint 16777216
    %uint_20 = OpConstant %uint 20
        %574 = OpConstantComposite %v2uint %uint_20 %uint_24
     %v3uint = OpTypeVector %uint 3
%uint_4294901760 = OpConstant %uint 4294901760
 %uint_65535 = OpConstant %uint 65535
    %uint_40 = OpConstant %uint 40
   %float_n1 = OpConstant %float -1
    %float_0 = OpConstant %float 0
  %float_0_5 = OpConstant %float 0.5
 %float_n0_5 = OpConstant %float -0.5
%float_32767 = OpConstant %float 32767
    %v2float = OpTypeVector %float 2
%xe_resolve_host_color_xe_block = OpTypeStruct %uint %uint %uint %uint %uint %uint %uint %uint %uint %uint %uint
%_ptr_Uniform_xe_resolve_host_color_xe_block = OpTypePointer Uniform %xe_resolve_host_color_xe_block
%xe_resolve_host_color = OpVariable %_ptr_Uniform_xe_resolve_host_color_xe_block Uniform
%_ptr_Uniform_uint = OpTypePointer Uniform %uint
        %862 = OpConstantComposite %v2uint %uint_1 %uint_0
        %893 = OpTypeImage %float 2D 0 0 1 1 Unknown
%_ptr_UniformConstant_893 = OpTypePointer UniformConstant %893
%xe_resolve_host_color_source = OpVariable %_ptr_UniformConstant_893 UniformConstant
      %false = OpConstantFalse %bool
       %true = OpConstantTrue %bool
       %1061 = OpConstantComposite %v2uint %uint_0 %uint_1
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%xe_resolve_dest_xe_block = OpTypeStruct %_runtimearr_v4uint
%_ptr_StorageBuffer_xe_resolve_dest_xe_block = OpTypePointer StorageBuffer %xe_resolve_dest_xe_block
%xe_resolve_dest = OpVariable %_ptr_StorageBuffer_xe_resolve_dest_xe_block StorageBuffer
%_ptr_StorageBuffer_v4uint = OpTypePointer StorageBuffer %v4uint
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %3646 = OpUndef %v2uint
       %3707 = OpConstantComposite %v2uint %uint_7 %uint_7
       %3708 = OpConstantComposite %v2uint %uint_1 %uint_1
       %3709 = OpConstantComposite %v2uint %uint_0 %uint_0
       %3710 = OpConstantComposite %v2uint %uint_3 %uint_3
       %3711 = OpConstantComposite %v2uint %uint_15 %uint_15
       %3712 = OpConstantComposite %v4uint %uint_4294901760 %uint_4294901760 %uint_4294901760 %uint_4294901760
       %3713 = OpConstantComposite %v4uint %uint_65535 %uint_65535 %uint_65535 %uint_65535
       %3714 = OpConstantComposite %v2uint %uint_2 %uint_1
     %uint_6 = OpConstant %uint 6
       %3716 = OpConstantComposite %v4uint %uint_16711935 %uint_16711935 %uint_16711935 %uint_16711935
       %3717 = OpConstantComposite %v4uint %uint_8 %uint_8 %uint_8 %uint_8
       %3718 = OpConstantComposite %v4uint %uint_4278255360 %uint_4278255360 %uint_4278255360 %uint_4278255360
       %3719 = OpConstantComposite %v4uint %uint_16 %uint_16 %uint_16 %uint_16
       %main = OpFunction %void None %3
          %5 = OpLabel
       %1274 = OpLoad %v3uint %gl_GlobalInvocationID
               OpSelectionMerge %1439 None
               OpSwitch %uint_0 %1347
       %1347 = OpLabel
       %1452 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_0
       %1453 = OpLoad %uint %1452
       %1454 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_1
       %1455 = OpLoad %uint %1454
       %1472 = OpShiftRightLogical %uint %1453 %uint_24
       %1473 = OpBitwiseAnd %uint %1472 %uint_15
       %1580 = OpCompositeConstruct %v2uint %1455 %1455
       %1481 = OpShiftRightLogical %v2uint %1580 %470
       %1483 = OpBitwiseAnd %v2uint %1481 %3707
       %1486 = OpBitwiseAnd %uint %1453 %uint_536870912
       %1487 = OpINotEqual %bool %1486 %uint_0
               OpSelectionMerge %1497 None
               OpBranchConditional %1487 %1488 %1494
       %1494 = OpLabel
               OpBranch %1497
       %1488 = OpLabel
       %1492 = OpShiftRightLogical %v2uint %1483 %3708
               OpBranch %1497
       %1497 = OpLabel
       %3641 = OpPhi %v2uint %1492 %1488 %3709 %1494
       %1500 = OpShiftRightLogical %v2uint %1580 %494
       %1502 = OpShiftLeftLogical %v2uint %3708 %498
       %1504 = OpISub %v2uint %1502 %3708
       %1505 = OpBitwiseAnd %v2uint %1500 %1504
       %1507 = OpShiftLeftLogical %v2uint %1505 %3710
       %1510 = OpIMul %v2uint %1507 %1483
       %1513 = OpShiftRightLogical %uint %1455 %uint_5
       %1514 = OpBitwiseAnd %uint %1513 %uint_2047
       %1516 = OpCompositeExtract %uint %1483 0
       %1517 = OpIMul %uint %1514 %1516
       %1519 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_2
       %1520 = OpLoad %uint %1519
       %1521 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_3
       %1522 = OpLoad %uint %1521
       %1524 = OpBitwiseAnd %uint %1520 %uint_7
       %1527 = OpBitwiseAnd %uint %1520 %uint_8
       %1528 = OpINotEqual %bool %1527 %uint_0
       %1531 = OpShiftRightLogical %uint %1520 %uint_4
       %1532 = OpBitwiseAnd %uint %1531 %uint_7
       %1548 = OpBitwiseAnd %uint %1520 %uint_16777216
       %1549 = OpINotEqual %bool %1548 %uint_0
       %1552 = OpBitwiseAnd %uint %1522 %uint_1023
       %1555 = OpShiftRightLogical %uint %1522 %uint_10
       %1556 = OpBitwiseAnd %uint %1555 %uint_1023
       %1557 = OpShiftLeftLogical %uint %1556 %int_1
       %1600 = OpCompositeConstruct %v2uint %1522 %1522
       %1561 = OpShiftRightLogical %v2uint %1600 %574
       %1563 = OpBitwiseAnd %v2uint %1561 %3711
       %1565 = OpShiftLeftLogical %v2uint %1563 %3710
       %1568 = OpIMul %v2uint %1565 %1483
       %1571 = OpShiftRightLogical %uint %1522 %uint_28
       %1572 = OpBitwiseAnd %uint %1571 %uint_7
               OpSelectionMerge %1732 None
               OpSwitch %uint_0 %1621
       %1621 = OpLabel
       %1623 = OpCompositeExtract %uint %1274 0
       %1624 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_5
       %1625 = OpLoad %uint %1624
       %1626 = OpUGreaterThanEqual %bool %1623 %1625
       %1627 = OpLogicalNot %bool %1626
               OpSelectionMerge %1634 None
               OpBranchConditional %1627 %1628 %1634
       %1628 = OpLabel
       %1630 = OpCompositeExtract %uint %1274 1
       %1631 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_6
       %1632 = OpLoad %uint %1631
       %1633 = OpUGreaterThanEqual %bool %1630 %1632
               OpBranch %1634
       %1634 = OpLabel
       %1635 = OpPhi %bool %1626 %1621 %1633 %1628
               OpSelectionMerge %1637 None
               OpBranchConditional %1635 %1636 %1637
       %1636 = OpLabel
               OpBranch %1732
       %1637 = OpLabel
       %1745 = OpIMul %uint %uint_40 %1516
       %1755 = OpCompositeExtract %uint %1483 1
       %1756 = OpIMul %uint %uint_16 %1755
       %1751 = OpShiftRightLogical %uint %1756 %uint_1
       %1646 = OpIMul %uint %1623 %uint_4
       %1648 = OpCompositeExtract %uint %1274 1
       %1651 = OpUDiv %uint %1646 %1745
       %1654 = OpUDiv %uint %1648 %1751
       %1658 = OpIMul %uint %1651 %1745
       %1659 = OpISub %uint %1646 %1658
       %1663 = OpIMul %uint %1654 %1751
       %1664 = OpISub %uint %1648 %1663
       %1665 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_0
       %1666 = OpLoad %uint %1665
       %1668 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_2
       %1669 = OpLoad %uint %1668
       %1670 = OpIMul %uint %1654 %1669
       %1671 = OpIAdd %uint %1666 %1670
       %1673 = OpIAdd %uint %1671 %1651
       %1678 = OpUDiv %uint %1673 %1669
       %1682 = OpIMul %uint %1678 %1669
       %1683 = OpISub %uint %1673 %1682
       %1686 = OpIMul %uint %1683 %1745
       %1688 = OpIAdd %uint %1686 %1659
       %1691 = OpIMul %uint %1678 %1751
       %1693 = OpIAdd %uint %1691 %1664
       %1694 = OpCompositeConstruct %v2uint %1688 %1693
       %1698 = OpCompositeExtract %uint %1510 0
       %1699 = OpULessThan %bool %1688 %1698
       %1700 = OpLogicalNot %bool %1699
               OpSelectionMerge %1707 None
               OpBranchConditional %1700 %1701 %1707
       %1701 = OpLabel
       %1705 = OpCompositeExtract %uint %1510 1
       %1706 = OpULessThan %bool %1693 %1705
               OpBranch %1707
       %1707 = OpLabel
       %1708 = OpPhi %bool %1699 %1637 %1706 %1701
               OpSelectionMerge %1710 None
               OpBranchConditional %1708 %1709 %1710
       %1709 = OpLabel
               OpBranch %1732
       %1710 = OpLabel
       %1714 = OpISub %v2uint %1694 %1510
       %1716 = OpCompositeExtract %uint %1714 0
       %1719 = OpShiftLeftLogical %uint %1517 %uint_3
       %1720 = OpUGreaterThanEqual %bool %1716 %1719
       %1721 = OpLogicalNot %bool %1720
               OpSelectionMerge %1728 None
               OpBranchConditional %1721 %1722 %1728
       %1722 = OpLabel
       %1724 = OpCompositeExtract %uint %1714 1
       %1725 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_7
       %1726 = OpLoad %uint %1725
       %1727 = OpUGreaterThanEqual %bool %1724 %1726
               OpBranch %1728
       %1728 = OpLabel
       %1729 = OpPhi %bool %1720 %1710 %1727 %1722
               OpSelectionMerge %1731 None
               OpBranchConditional %1729 %1730 %1731
       %1730 = OpLabel
               OpBranch %1732
       %1731 = OpLabel
               OpBranch %1732
       %1732 = OpLabel
       %3643 = OpPhi %v2uint %3646 %1636 %3646 %1709 %1714 %1730 %1714 %1731
       %3642 = OpPhi %bool %false %1636 %false %1709 %false %1730 %true %1731
       %1353 = OpLogicalNot %bool %3642
               OpSelectionMerge %1355 None
               OpBranchConditional %1353 %1354 %1355
       %1354 = OpLabel
               OpBranch %1439
       %1355 = OpLabel
       %1775 = OpULessThanEqual %bool %1572 %uint_3
               OpSelectionMerge %1784 None
               OpBranchConditional %1775 %1776 %1778
       %1778 = OpLabel
       %1780 = OpIEqual %bool %1572 %uint_5
       %3721 = OpSelect %uint %1780 %uint_2 %uint_0
               OpBranch %1784
       %1776 = OpLabel
               OpBranch %1784
       %1784 = OpLabel
       %3649 = OpPhi %uint %1572 %1776 %3721 %1778
       %1791 = OpCompositeExtract %uint %3643 0
       %1795 = OpCompositeExtract %uint %3643 1
       %1797 = OpCompositeExtract %uint %3641 1
       %1798 = OpExtInst %uint %1 UMax %1795 %1797
       %1799 = OpCompositeConstruct %v2uint %1791 %1798
       %1802 = OpIAdd %v2uint %1799 %1510
       %1804 = OpShiftLeftLogical %v2uint %1802 %1061
       %1820 = OpCompositeConstruct %v2uint %3649 %3649
       %1813 = OpShiftRightLogical %v2uint %1820 %862
       %1815 = OpBitwiseAnd %v2uint %1813 %3708
       %1807 = OpIAdd %v2uint %1804 %1815
       %1928 = OpIMul %uint %uint_40 %1516
       %1932 = OpCompositeExtract %uint %1483 1
       %1933 = OpIMul %uint %uint_16 %1932
       %1870 = OpCompositeExtract %uint %1807 0
       %1872 = OpUDiv %uint %1870 %1928
       %1874 = OpCompositeExtract %uint %1807 1
       %1876 = OpUDiv %uint %1874 %1933
       %1881 = OpIMul %uint %1872 %1928
       %1882 = OpISub %uint %1870 %1881
       %1887 = OpIMul %uint %1876 %1933
       %1888 = OpISub %uint %1874 %1887
       %1890 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_2
       %1891 = OpLoad %uint %1890
       %1892 = OpIMul %uint %1876 %1891
       %1894 = OpIAdd %uint %1892 %1872
       %1895 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_1
       %1896 = OpLoad %uint %1895
       %1898 = OpIAdd %uint %1896 %1894
       %1900 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_3
       %1901 = OpLoad %uint %1900
       %1902 = OpISub %uint %1898 %1901
       %1903 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_4
       %1904 = OpLoad %uint %1903
       %1907 = OpUDiv %uint %1902 %1904
       %1911 = OpIMul %uint %1907 %1904
       %1912 = OpISub %uint %1902 %1911
       %1915 = OpIMul %uint %1912 %1928
       %1917 = OpIAdd %uint %1915 %1882
       %1920 = OpIMul %uint %1907 %1933
       %1922 = OpIAdd %uint %1920 %1888
       %1938 = OpBitwiseAnd %uint %1922 %uint_1
       %1939 = OpINotEqual %bool %1938 %uint_0
               OpSelectionMerge %1946 None
               OpBranchConditional %1939 %1940 %1943
       %1943 = OpLabel
       %1944 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %1945 = OpLoad %uint %1944
               OpBranch %1946
       %1940 = OpLabel
       %1941 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %1942 = OpLoad %uint %1941
               OpBranch %1946
       %1946 = OpLabel
       %3651 = OpPhi %uint %1942 %1940 %1945 %1943
       %1834 = OpLoad %893 %xe_resolve_host_color_source
       %1837 = OpBitcast %int %1917
       %1840 = OpShiftRightLogical %uint %1922 %uint_1
       %1841 = OpBitcast %int %1840
       %1845 = OpCompositeConstruct %v2int %1837 %1841
       %1847 = OpBitcast %int %3651
       %1848 = OpImageFetch %v4float %1834 %1845 Sample %1847
               OpSelectionMerge %1988 None
               OpSwitch %1473 %1958 5 %1962 7 %1980
       %1980 = OpLabel
       %1982 = OpVectorShuffle %v2float %1848 %1848 0 1
       %1983 = OpExtInst %uint %1 PackHalf2x16 %1982
       %1985 = OpVectorShuffle %v2float %1848 %1848 2 3
       %1986 = OpExtInst %uint %1 PackHalf2x16 %1985
       %1987 = OpCompositeConstruct %v2uint %1983 %1986
               OpBranch %1988
       %1962 = OpLabel
       %1964 = OpCompositeExtract %float %1848 0
       %1998 = OpExtInst %float %1 FMax %1964 %float_n1
       %1999 = OpExtInst %float %1 FMin %1998 %float_1
       %2001 = OpFOrdGreaterThanEqual %bool %1999 %float_0
       %2002 = OpSelect %float %2001 %float_0_5 %float_n0_5
       %2006 = OpExtInst %float %1 Fma %1999 %float_32767 %2002
       %2007 = OpConvertFToS %int %2006
       %2008 = OpBitcast %uint %2007
       %2009 = OpBitwiseAnd %uint %2008 %uint_65535
       %1967 = OpCompositeExtract %float %1848 1
       %2015 = OpExtInst %float %1 FMax %1967 %float_n1
       %2016 = OpExtInst %float %1 FMin %2015 %float_1
       %2018 = OpFOrdGreaterThanEqual %bool %2016 %float_0
       %2019 = OpSelect %float %2018 %float_0_5 %float_n0_5
       %2023 = OpExtInst %float %1 Fma %2016 %float_32767 %2019
       %2024 = OpConvertFToS %int %2023
       %2025 = OpBitcast %uint %2024
       %2026 = OpBitwiseAnd %uint %2025 %uint_65535
       %1969 = OpShiftLeftLogical %uint %2026 %uint_16
       %1970 = OpBitwiseOr %uint %2009 %1969
       %1972 = OpCompositeExtract %float %1848 2
       %2032 = OpExtInst %float %1 FMax %1972 %float_n1
       %2033 = OpExtInst %float %1 FMin %2032 %float_1
       %2035 = OpFOrdGreaterThanEqual %bool %2033 %float_0
       %2036 = OpSelect %float %2035 %float_0_5 %float_n0_5
       %2040 = OpExtInst %float %1 Fma %2033 %float_32767 %2036
       %2041 = OpConvertFToS %int %2040
       %2042 = OpBitcast %uint %2041
       %2043 = OpBitwiseAnd %uint %2042 %uint_65535
       %1975 = OpCompositeExtract %float %1848 3
       %2049 = OpExtInst %float %1 FMax %1975 %float_n1
       %2050 = OpExtInst %float %1 FMin %2049 %float_1
       %2052 = OpFOrdGreaterThanEqual %bool %2050 %float_0
       %2053 = OpSelect %float %2052 %float_0_5 %float_n0_5
       %2057 = OpExtInst %float %1 Fma %2050 %float_32767 %2053
       %2058 = OpConvertFToS %int %2057
       %2059 = OpBitcast %uint %2058
       %2060 = OpBitwiseAnd %uint %2059 %uint_65535
       %1977 = OpShiftLeftLogical %uint %2060 %uint_16
       %1978 = OpBitwiseOr %uint %2043 %1977
       %1979 = OpCompositeConstruct %v2uint %1970 %1978
               OpBranch %1988
       %1958 = OpLabel
       %1960 = OpVectorShuffle %v2float %1848 %1848 0 1
       %1961 = OpBitcast %v2uint %1960
               OpBranch %1988
       %1988 = OpLabel
       %3654 = OpPhi %v2uint %1961 %1958 %1979 %1962 %1987 %1980
               OpSelectionMerge %2088 None
               OpBranchConditional %1775 %2080 %2082
       %2082 = OpLabel
       %2084 = OpIEqual %bool %1572 %uint_5
       %3722 = OpSelect %uint %2084 %uint_2 %uint_0
               OpBranch %2088
       %2080 = OpLabel
               OpBranch %2088
       %2088 = OpLabel
       %3655 = OpPhi %uint %1572 %2080 %3722 %2082
       %2097 = OpIAdd %uint %1791 %uint_1
       %2103 = OpCompositeConstruct %v2uint %2097 %1798
       %2106 = OpIAdd %v2uint %2103 %1510
       %2108 = OpShiftLeftLogical %v2uint %2106 %1061
       %2124 = OpCompositeConstruct %v2uint %3655 %3655
       %2117 = OpShiftRightLogical %v2uint %2124 %862
       %2119 = OpBitwiseAnd %v2uint %2117 %3708
       %2111 = OpIAdd %v2uint %2108 %2119
       %2174 = OpCompositeExtract %uint %2111 0
       %2176 = OpUDiv %uint %2174 %1928
       %2178 = OpCompositeExtract %uint %2111 1
       %2180 = OpUDiv %uint %2178 %1933
       %2185 = OpIMul %uint %2176 %1928
       %2186 = OpISub %uint %2174 %2185
       %2191 = OpIMul %uint %2180 %1933
       %2192 = OpISub %uint %2178 %2191
       %2196 = OpIMul %uint %2180 %1891
       %2198 = OpIAdd %uint %2196 %2176
       %2202 = OpIAdd %uint %1896 %2198
       %2206 = OpISub %uint %2202 %1901
       %2211 = OpUDiv %uint %2206 %1904
       %2215 = OpIMul %uint %2211 %1904
       %2216 = OpISub %uint %2206 %2215
       %2219 = OpIMul %uint %2216 %1928
       %2221 = OpIAdd %uint %2219 %2186
       %2224 = OpIMul %uint %2211 %1933
       %2226 = OpIAdd %uint %2224 %2192
       %2242 = OpBitwiseAnd %uint %2226 %uint_1
       %2243 = OpINotEqual %bool %2242 %uint_0
               OpSelectionMerge %2250 None
               OpBranchConditional %2243 %2244 %2247
       %2247 = OpLabel
       %2248 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2249 = OpLoad %uint %2248
               OpBranch %2250
       %2244 = OpLabel
       %2245 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2246 = OpLoad %uint %2245
               OpBranch %2250
       %2250 = OpLabel
       %3657 = OpPhi %uint %2246 %2244 %2249 %2247
       %2141 = OpBitcast %int %2221
       %2144 = OpShiftRightLogical %uint %2226 %uint_1
       %2145 = OpBitcast %int %2144
       %2149 = OpCompositeConstruct %v2int %2141 %2145
       %2151 = OpBitcast %int %3657
       %2152 = OpImageFetch %v4float %1834 %2149 Sample %2151
               OpSelectionMerge %2292 None
               OpSwitch %1473 %2262 5 %2266 7 %2284
       %2284 = OpLabel
       %2286 = OpVectorShuffle %v2float %2152 %2152 0 1
       %2287 = OpExtInst %uint %1 PackHalf2x16 %2286
       %2289 = OpVectorShuffle %v2float %2152 %2152 2 3
       %2290 = OpExtInst %uint %1 PackHalf2x16 %2289
       %2291 = OpCompositeConstruct %v2uint %2287 %2290
               OpBranch %2292
       %2266 = OpLabel
       %2268 = OpCompositeExtract %float %2152 0
       %2302 = OpExtInst %float %1 FMax %2268 %float_n1
       %2303 = OpExtInst %float %1 FMin %2302 %float_1
       %2305 = OpFOrdGreaterThanEqual %bool %2303 %float_0
       %2306 = OpSelect %float %2305 %float_0_5 %float_n0_5
       %2310 = OpExtInst %float %1 Fma %2303 %float_32767 %2306
       %2311 = OpConvertFToS %int %2310
       %2312 = OpBitcast %uint %2311
       %2313 = OpBitwiseAnd %uint %2312 %uint_65535
       %2271 = OpCompositeExtract %float %2152 1
       %2319 = OpExtInst %float %1 FMax %2271 %float_n1
       %2320 = OpExtInst %float %1 FMin %2319 %float_1
       %2322 = OpFOrdGreaterThanEqual %bool %2320 %float_0
       %2323 = OpSelect %float %2322 %float_0_5 %float_n0_5
       %2327 = OpExtInst %float %1 Fma %2320 %float_32767 %2323
       %2328 = OpConvertFToS %int %2327
       %2329 = OpBitcast %uint %2328
       %2330 = OpBitwiseAnd %uint %2329 %uint_65535
       %2273 = OpShiftLeftLogical %uint %2330 %uint_16
       %2274 = OpBitwiseOr %uint %2313 %2273
       %2276 = OpCompositeExtract %float %2152 2
       %2336 = OpExtInst %float %1 FMax %2276 %float_n1
       %2337 = OpExtInst %float %1 FMin %2336 %float_1
       %2339 = OpFOrdGreaterThanEqual %bool %2337 %float_0
       %2340 = OpSelect %float %2339 %float_0_5 %float_n0_5
       %2344 = OpExtInst %float %1 Fma %2337 %float_32767 %2340
       %2345 = OpConvertFToS %int %2344
       %2346 = OpBitcast %uint %2345
       %2347 = OpBitwiseAnd %uint %2346 %uint_65535
       %2279 = OpCompositeExtract %float %2152 3
       %2353 = OpExtInst %float %1 FMax %2279 %float_n1
       %2354 = OpExtInst %float %1 FMin %2353 %float_1
       %2356 = OpFOrdGreaterThanEqual %bool %2354 %float_0
       %2357 = OpSelect %float %2356 %float_0_5 %float_n0_5
       %2361 = OpExtInst %float %1 Fma %2354 %float_32767 %2357
       %2362 = OpConvertFToS %int %2361
       %2363 = OpBitcast %uint %2362
       %2364 = OpBitwiseAnd %uint %2363 %uint_65535
       %2281 = OpShiftLeftLogical %uint %2364 %uint_16
       %2282 = OpBitwiseOr %uint %2347 %2281
       %2283 = OpCompositeConstruct %v2uint %2274 %2282
               OpBranch %2292
       %2262 = OpLabel
       %2264 = OpVectorShuffle %v2float %2152 %2152 0 1
       %2265 = OpBitcast %v2uint %2264
               OpBranch %2292
       %2292 = OpLabel
       %3660 = OpPhi %v2uint %2265 %2262 %2283 %2266 %2291 %2284
               OpSelectionMerge %2392 None
               OpBranchConditional %1775 %2384 %2386
       %2386 = OpLabel
       %2388 = OpIEqual %bool %1572 %uint_5
       %3723 = OpSelect %uint %2388 %uint_2 %uint_0
               OpBranch %2392
       %2384 = OpLabel
               OpBranch %2392
       %2392 = OpLabel
       %3661 = OpPhi %uint %1572 %2384 %3723 %2386
       %2401 = OpIAdd %uint %1791 %uint_2
       %2407 = OpCompositeConstruct %v2uint %2401 %1798
       %2410 = OpIAdd %v2uint %2407 %1510
       %2412 = OpShiftLeftLogical %v2uint %2410 %1061
       %2428 = OpCompositeConstruct %v2uint %3661 %3661
       %2421 = OpShiftRightLogical %v2uint %2428 %862
       %2423 = OpBitwiseAnd %v2uint %2421 %3708
       %2415 = OpIAdd %v2uint %2412 %2423
       %2478 = OpCompositeExtract %uint %2415 0
       %2480 = OpUDiv %uint %2478 %1928
       %2482 = OpCompositeExtract %uint %2415 1
       %2484 = OpUDiv %uint %2482 %1933
       %2489 = OpIMul %uint %2480 %1928
       %2490 = OpISub %uint %2478 %2489
       %2495 = OpIMul %uint %2484 %1933
       %2496 = OpISub %uint %2482 %2495
       %2500 = OpIMul %uint %2484 %1891
       %2502 = OpIAdd %uint %2500 %2480
       %2506 = OpIAdd %uint %1896 %2502
       %2510 = OpISub %uint %2506 %1901
       %2515 = OpUDiv %uint %2510 %1904
       %2519 = OpIMul %uint %2515 %1904
       %2520 = OpISub %uint %2510 %2519
       %2523 = OpIMul %uint %2520 %1928
       %2525 = OpIAdd %uint %2523 %2490
       %2528 = OpIMul %uint %2515 %1933
       %2530 = OpIAdd %uint %2528 %2496
       %2546 = OpBitwiseAnd %uint %2530 %uint_1
       %2547 = OpINotEqual %bool %2546 %uint_0
               OpSelectionMerge %2554 None
               OpBranchConditional %2547 %2548 %2551
       %2551 = OpLabel
       %2552 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2553 = OpLoad %uint %2552
               OpBranch %2554
       %2548 = OpLabel
       %2549 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2550 = OpLoad %uint %2549
               OpBranch %2554
       %2554 = OpLabel
       %3663 = OpPhi %uint %2550 %2548 %2553 %2551
       %2445 = OpBitcast %int %2525
       %2448 = OpShiftRightLogical %uint %2530 %uint_1
       %2449 = OpBitcast %int %2448
       %2453 = OpCompositeConstruct %v2int %2445 %2449
       %2455 = OpBitcast %int %3663
       %2456 = OpImageFetch %v4float %1834 %2453 Sample %2455
               OpSelectionMerge %2596 None
               OpSwitch %1473 %2566 5 %2570 7 %2588
       %2588 = OpLabel
       %2590 = OpVectorShuffle %v2float %2456 %2456 0 1
       %2591 = OpExtInst %uint %1 PackHalf2x16 %2590
       %2593 = OpVectorShuffle %v2float %2456 %2456 2 3
       %2594 = OpExtInst %uint %1 PackHalf2x16 %2593
       %2595 = OpCompositeConstruct %v2uint %2591 %2594
               OpBranch %2596
       %2570 = OpLabel
       %2572 = OpCompositeExtract %float %2456 0
       %2606 = OpExtInst %float %1 FMax %2572 %float_n1
       %2607 = OpExtInst %float %1 FMin %2606 %float_1
       %2609 = OpFOrdGreaterThanEqual %bool %2607 %float_0
       %2610 = OpSelect %float %2609 %float_0_5 %float_n0_5
       %2614 = OpExtInst %float %1 Fma %2607 %float_32767 %2610
       %2615 = OpConvertFToS %int %2614
       %2616 = OpBitcast %uint %2615
       %2617 = OpBitwiseAnd %uint %2616 %uint_65535
       %2575 = OpCompositeExtract %float %2456 1
       %2623 = OpExtInst %float %1 FMax %2575 %float_n1
       %2624 = OpExtInst %float %1 FMin %2623 %float_1
       %2626 = OpFOrdGreaterThanEqual %bool %2624 %float_0
       %2627 = OpSelect %float %2626 %float_0_5 %float_n0_5
       %2631 = OpExtInst %float %1 Fma %2624 %float_32767 %2627
       %2632 = OpConvertFToS %int %2631
       %2633 = OpBitcast %uint %2632
       %2634 = OpBitwiseAnd %uint %2633 %uint_65535
       %2577 = OpShiftLeftLogical %uint %2634 %uint_16
       %2578 = OpBitwiseOr %uint %2617 %2577
       %2580 = OpCompositeExtract %float %2456 2
       %2640 = OpExtInst %float %1 FMax %2580 %float_n1
       %2641 = OpExtInst %float %1 FMin %2640 %float_1
       %2643 = OpFOrdGreaterThanEqual %bool %2641 %float_0
       %2644 = OpSelect %float %2643 %float_0_5 %float_n0_5
       %2648 = OpExtInst %float %1 Fma %2641 %float_32767 %2644
       %2649 = OpConvertFToS %int %2648
       %2650 = OpBitcast %uint %2649
       %2651 = OpBitwiseAnd %uint %2650 %uint_65535
       %2583 = OpCompositeExtract %float %2456 3
       %2657 = OpExtInst %float %1 FMax %2583 %float_n1
       %2658 = OpExtInst %float %1 FMin %2657 %float_1
       %2660 = OpFOrdGreaterThanEqual %bool %2658 %float_0
       %2661 = OpSelect %float %2660 %float_0_5 %float_n0_5
       %2665 = OpExtInst %float %1 Fma %2658 %float_32767 %2661
       %2666 = OpConvertFToS %int %2665
       %2667 = OpBitcast %uint %2666
       %2668 = OpBitwiseAnd %uint %2667 %uint_65535
       %2585 = OpShiftLeftLogical %uint %2668 %uint_16
       %2586 = OpBitwiseOr %uint %2651 %2585
       %2587 = OpCompositeConstruct %v2uint %2578 %2586
               OpBranch %2596
       %2566 = OpLabel
       %2568 = OpVectorShuffle %v2float %2456 %2456 0 1
       %2569 = OpBitcast %v2uint %2568
               OpBranch %2596
       %2596 = OpLabel
       %3666 = OpPhi %v2uint %2569 %2566 %2587 %2570 %2595 %2588
               OpSelectionMerge %2696 None
               OpBranchConditional %1775 %2688 %2690
       %2690 = OpLabel
       %2692 = OpIEqual %bool %1572 %uint_5
       %3724 = OpSelect %uint %2692 %uint_2 %uint_0
               OpBranch %2696
       %2688 = OpLabel
               OpBranch %2696
       %2696 = OpLabel
       %3667 = OpPhi %uint %1572 %2688 %3724 %2690
       %2705 = OpIAdd %uint %1791 %uint_3
       %2711 = OpCompositeConstruct %v2uint %2705 %1798
       %2714 = OpIAdd %v2uint %2711 %1510
       %2716 = OpShiftLeftLogical %v2uint %2714 %1061
       %2732 = OpCompositeConstruct %v2uint %3667 %3667
       %2725 = OpShiftRightLogical %v2uint %2732 %862
       %2727 = OpBitwiseAnd %v2uint %2725 %3708
       %2719 = OpIAdd %v2uint %2716 %2727
       %2782 = OpCompositeExtract %uint %2719 0
       %2784 = OpUDiv %uint %2782 %1928
       %2786 = OpCompositeExtract %uint %2719 1
       %2788 = OpUDiv %uint %2786 %1933
       %2793 = OpIMul %uint %2784 %1928
       %2794 = OpISub %uint %2782 %2793
       %2799 = OpIMul %uint %2788 %1933
       %2800 = OpISub %uint %2786 %2799
       %2804 = OpIMul %uint %2788 %1891
       %2806 = OpIAdd %uint %2804 %2784
       %2810 = OpIAdd %uint %1896 %2806
       %2814 = OpISub %uint %2810 %1901
       %2819 = OpUDiv %uint %2814 %1904
       %2823 = OpIMul %uint %2819 %1904
       %2824 = OpISub %uint %2814 %2823
       %2827 = OpIMul %uint %2824 %1928
       %2829 = OpIAdd %uint %2827 %2794
       %2832 = OpIMul %uint %2819 %1933
       %2834 = OpIAdd %uint %2832 %2800
       %2850 = OpBitwiseAnd %uint %2834 %uint_1
       %2851 = OpINotEqual %bool %2850 %uint_0
               OpSelectionMerge %2858 None
               OpBranchConditional %2851 %2852 %2855
       %2855 = OpLabel
       %2856 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2857 = OpLoad %uint %2856
               OpBranch %2858
       %2852 = OpLabel
       %2853 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2854 = OpLoad %uint %2853
               OpBranch %2858
       %2858 = OpLabel
       %3669 = OpPhi %uint %2854 %2852 %2857 %2855
       %2749 = OpBitcast %int %2829
       %2752 = OpShiftRightLogical %uint %2834 %uint_1
       %2753 = OpBitcast %int %2752
       %2757 = OpCompositeConstruct %v2int %2749 %2753
       %2759 = OpBitcast %int %3669
       %2760 = OpImageFetch %v4float %1834 %2757 Sample %2759
               OpSelectionMerge %2900 None
               OpSwitch %1473 %2870 5 %2874 7 %2892
       %2892 = OpLabel
       %2894 = OpVectorShuffle %v2float %2760 %2760 0 1
       %2895 = OpExtInst %uint %1 PackHalf2x16 %2894
       %2897 = OpVectorShuffle %v2float %2760 %2760 2 3
       %2898 = OpExtInst %uint %1 PackHalf2x16 %2897
       %2899 = OpCompositeConstruct %v2uint %2895 %2898
               OpBranch %2900
       %2874 = OpLabel
       %2876 = OpCompositeExtract %float %2760 0
       %2910 = OpExtInst %float %1 FMax %2876 %float_n1
       %2911 = OpExtInst %float %1 FMin %2910 %float_1
       %2913 = OpFOrdGreaterThanEqual %bool %2911 %float_0
       %2914 = OpSelect %float %2913 %float_0_5 %float_n0_5
       %2918 = OpExtInst %float %1 Fma %2911 %float_32767 %2914
       %2919 = OpConvertFToS %int %2918
       %2920 = OpBitcast %uint %2919
       %2921 = OpBitwiseAnd %uint %2920 %uint_65535
       %2879 = OpCompositeExtract %float %2760 1
       %2927 = OpExtInst %float %1 FMax %2879 %float_n1
       %2928 = OpExtInst %float %1 FMin %2927 %float_1
       %2930 = OpFOrdGreaterThanEqual %bool %2928 %float_0
       %2931 = OpSelect %float %2930 %float_0_5 %float_n0_5
       %2935 = OpExtInst %float %1 Fma %2928 %float_32767 %2931
       %2936 = OpConvertFToS %int %2935
       %2937 = OpBitcast %uint %2936
       %2938 = OpBitwiseAnd %uint %2937 %uint_65535
       %2881 = OpShiftLeftLogical %uint %2938 %uint_16
       %2882 = OpBitwiseOr %uint %2921 %2881
       %2884 = OpCompositeExtract %float %2760 2
       %2944 = OpExtInst %float %1 FMax %2884 %float_n1
       %2945 = OpExtInst %float %1 FMin %2944 %float_1
       %2947 = OpFOrdGreaterThanEqual %bool %2945 %float_0
       %2948 = OpSelect %float %2947 %float_0_5 %float_n0_5
       %2952 = OpExtInst %float %1 Fma %2945 %float_32767 %2948
       %2953 = OpConvertFToS %int %2952
       %2954 = OpBitcast %uint %2953
       %2955 = OpBitwiseAnd %uint %2954 %uint_65535
       %2887 = OpCompositeExtract %float %2760 3
       %2961 = OpExtInst %float %1 FMax %2887 %float_n1
       %2962 = OpExtInst %float %1 FMin %2961 %float_1
       %2964 = OpFOrdGreaterThanEqual %bool %2962 %float_0
       %2965 = OpSelect %float %2964 %float_0_5 %float_n0_5
       %2969 = OpExtInst %float %1 Fma %2962 %float_32767 %2965
       %2970 = OpConvertFToS %int %2969
       %2971 = OpBitcast %uint %2970
       %2972 = OpBitwiseAnd %uint %2971 %uint_65535
       %2889 = OpShiftLeftLogical %uint %2972 %uint_16
       %2890 = OpBitwiseOr %uint %2955 %2889
       %2891 = OpCompositeConstruct %v2uint %2882 %2890
               OpBranch %2900
       %2870 = OpLabel
       %2872 = OpVectorShuffle %v2float %2760 %2760 0 1
       %2873 = OpBitcast %v2uint %2872
               OpBranch %2900
       %2900 = OpLabel
       %3672 = OpPhi %v2uint %2873 %2870 %2891 %2874 %2899 %2892
       %1389 = OpCompositeExtract %uint %3654 0
       %1391 = OpCompositeExtract %uint %3654 1
       %1393 = OpCompositeExtract %uint %3660 0
       %1395 = OpCompositeExtract %uint %3660 1
       %1396 = OpCompositeConstruct %v4uint %1389 %1391 %1393 %1395
       %1398 = OpCompositeExtract %uint %3666 0
       %1400 = OpCompositeExtract %uint %3666 1
       %1402 = OpCompositeExtract %uint %3672 0
       %1404 = OpCompositeExtract %uint %3672 1
       %1405 = OpCompositeConstruct %v4uint %1398 %1400 %1402 %1404
       %2976 = OpIEqual %bool %1791 %uint_0
               OpSelectionMerge %2981 None
               OpBranchConditional %2976 %2977 %2981
       %2977 = OpLabel
       %2979 = OpCompositeExtract %uint %3641 0
       %2980 = OpINotEqual %bool %2979 %uint_0
               OpBranch %2981
       %2981 = OpLabel
       %2982 = OpPhi %bool %2976 %2900 %2980 %2977
               OpSelectionMerge %3012 DontFlatten
               OpBranchConditional %2982 %2983 %3012
       %2983 = OpLabel
       %2985 = OpCompositeExtract %uint %3641 0
       %2986 = OpUGreaterThanEqual %bool %2985 %uint_2
               OpSelectionMerge %3005 None
               OpBranchConditional %2986 %2987 %3005
       %2987 = OpLabel
       %2990 = OpUGreaterThanEqual %bool %2985 %uint_3
               OpSelectionMerge %2998 None
               OpBranchConditional %2990 %2991 %2998
       %2991 = OpLabel
       %3624 = OpCompositeInsert %v4uint %1402 %1405 0
       %3626 = OpCompositeInsert %v4uint %1404 %3624 1
               OpBranch %2998
       %2998 = OpLabel
       %3674 = OpPhi %v4uint %1405 %2987 %3626 %2991
       %3002 = OpCompositeExtract %uint %3674 0
       %3628 = OpCompositeInsert %v4uint %3002 %1396 2
       %3004 = OpCompositeExtract %uint %3674 1
       %3630 = OpCompositeInsert %v4uint %3004 %3628 3
               OpBranch %3005
       %3005 = OpLabel
       %3680 = OpPhi %v4uint %1405 %2983 %3674 %2998
       %3677 = OpPhi %v4uint %1396 %2983 %3630 %2998
       %3009 = OpCompositeExtract %uint %3677 2
       %3632 = OpCompositeInsert %v4uint %3009 %3677 0
       %3011 = OpCompositeExtract %uint %3677 3
       %3634 = OpCompositeInsert %v4uint %3011 %3632 1
               OpBranch %3012
       %3012 = OpLabel
       %3679 = OpPhi %v4uint %1405 %2981 %3680 %3005
       %3678 = OpPhi %v4uint %1396 %2981 %3634 %3005
               OpSelectionMerge %3045 DontFlatten
               OpBranchConditional %1549 %3016 %3045
       %3016 = OpLabel
       %3019 = OpIEqual %bool %1473 %uint_5
       %3020 = OpLogicalNot %bool %3019
               OpSelectionMerge %3025 None
               OpBranchConditional %3020 %3021 %3025
       %3021 = OpLabel
       %3024 = OpIEqual %bool %1473 %uint_7
               OpBranch %3025
       %3025 = OpLabel
       %3026 = OpPhi %bool %3019 %3016 %3024 %3021
               OpSelectionMerge %3044 DontFlatten
               OpBranchConditional %3026 %3027 %3044
       %3027 = OpLabel
       %3030 = OpBitwiseAnd %v4uint %3678 %3712
       %3032 = OpVectorShuffle %v4uint %3678 %3678 1 0 3 2
       %3034 = OpBitwiseAnd %v4uint %3032 %3713
       %3035 = OpBitwiseOr %v4uint %3030 %3034
       %3038 = OpBitwiseAnd %v4uint %3679 %3712
       %3040 = OpVectorShuffle %v4uint %3679 %3679 1 0 3 2
       %3042 = OpBitwiseAnd %v4uint %3040 %3713
       %3043 = OpBitwiseOr %v4uint %3038 %3042
               OpBranch %3044
       %3044 = OpLabel
       %3686 = OpPhi %v4uint %3679 %3025 %3043 %3027
       %3684 = OpPhi %v4uint %3678 %3025 %3035 %3027
               OpBranch %3045
       %3045 = OpLabel
       %3685 = OpPhi %v4uint %3679 %3012 %3686 %3044
       %3683 = OpPhi %v4uint %3678 %3012 %3684 %3044
       %3053 = OpIAdd %v2uint %3643 %1568
       %3104 = OpShiftRightLogical %v2uint %3053 %3714
       %3106 = OpUDiv %v2uint %3104 %1483
       %3109 = OpIMul %v2uint %1483 %3106
       %3110 = OpISub %v2uint %3104 %3109
       %3113 = OpShiftLeftLogical %v2uint %3106 %3714
       %3116 = OpCompositeExtract %uint %3110 0
       %3118 = OpIMul %uint %3116 %1932
       %3120 = OpCompositeExtract %uint %3110 1
       %3121 = OpIAdd %uint %3118 %3120
       %3127 = OpShiftLeftLogical %v2uint %3708 %3714
       %3129 = OpISub %v2uint %3127 %3708
       %3130 = OpBitwiseAnd %v2uint %3053 %3129
       %3136 = OpShiftLeftLogical %uint %3121 %uint_6
       %3138 = OpCompositeExtract %uint %3130 1
       %3140 = OpShiftLeftLogical %uint %3138 %uint_5
       %3141 = OpBitwiseOr %uint %3136 %3140
       %3143 = OpCompositeExtract %uint %3130 0
       %3144 = OpShiftLeftLogical %uint %3143 %uint_3
       %3145 = OpBitwiseOr %uint %3141 %3144
               OpSelectionMerge %3078 DontFlatten
               OpBranchConditional %1528 %3061 %3072
       %3072 = OpLabel
       %3074 = OpBitcast %v2int %3113
       %3245 = OpCompositeExtract %int %3074 1
       %3246 = OpShiftRightArithmetic %int %3245 %int_5
       %3247 = OpBitcast %int %1552
       %3248 = OpIMul %int %3246 %3247
       %3249 = OpCompositeExtract %int %3074 0
       %3250 = OpShiftRightArithmetic %int %3249 %int_5
       %3251 = OpIAdd %int %3248 %3250
       %3252 = OpShiftLeftLogical %int %3251 %int_6
       %3254 = OpShiftRightArithmetic %int %3245 %int_1
       %3255 = OpBitwiseAnd %int %3254 %int_7
       %3256 = OpShiftLeftLogical %int %3255 %int_3
       %3258 = OpBitwiseAnd %int %3249 %int_7
       %3259 = OpBitwiseOr %int %3256 %3258
       %3262 = OpBitwiseOr %int %3252 %3259
       %3263 = OpShiftLeftLogical %int %3262 %uint_3
       %3265 = OpShiftRightArithmetic %int %3245 %int_4
       %3266 = OpBitwiseAnd %int %3265 %int_1
       %3268 = OpShiftRightArithmetic %int %3249 %int_3
       %3269 = OpBitwiseAnd %int %3268 %int_3
       %3271 = OpShiftRightArithmetic %int %3245 %int_3
       %3272 = OpBitwiseAnd %int %3271 %int_1
       %3273 = OpShiftLeftLogical %int %3272 %int_1
       %3274 = OpBitwiseXor %int %3269 %3273
       %3279 = OpBitwiseAnd %int %3245 %int_1
       %3283 = OpShiftLeftLogical %int %3279 %int_4
       %3284 = OpShiftLeftLogical %int %3274 %int_6
       %3285 = OpBitwiseOr %int %3283 %3284
       %3286 = OpShiftLeftLogical %int %3266 %int_11
       %3287 = OpBitwiseOr %int %3285 %3286
       %3288 = OpBitwiseAnd %int %3263 %int_15
       %3289 = OpBitwiseOr %int %3287 %3288
       %3290 = OpShiftRightArithmetic %int %3263 %int_4
       %3291 = OpBitwiseAnd %int %3290 %int_1
       %3292 = OpShiftLeftLogical %int %3291 %int_5
       %3293 = OpBitwiseOr %int %3289 %3292
       %3294 = OpShiftRightArithmetic %int %3263 %int_5
       %3295 = OpBitwiseAnd %int %3294 %int_7
       %3296 = OpShiftLeftLogical %int %3295 %int_8
       %3297 = OpBitwiseOr %int %3293 %3296
       %3298 = OpShiftRightArithmetic %int %3263 %int_8
       %3299 = OpShiftLeftLogical %int %3298 %int_12
       %3300 = OpBitwiseOr %int %3297 %3299
       %3077 = OpBitcast %uint %3300
               OpBranch %3078
       %3061 = OpLabel
       %3064 = OpCompositeExtract %uint %3113 0
       %3065 = OpCompositeExtract %uint %3113 1
       %3066 = OpCompositeConstruct %v3uint %3064 %3065 %1532
       %3067 = OpBitcast %v3int %3066
       %3172 = OpCompositeExtract %int %3067 2
       %3173 = OpShiftRightArithmetic %int %3172 %int_2
       %3174 = OpBitcast %int %1557
       %3175 = OpIMul %int %3173 %3174
       %3176 = OpCompositeExtract %int %3067 1
       %3177 = OpShiftRightArithmetic %int %3176 %int_4
       %3178 = OpIAdd %int %3175 %3177
       %3179 = OpBitcast %int %1552
       %3180 = OpIMul %int %3178 %3179
       %3181 = OpCompositeExtract %int %3067 0
       %3182 = OpShiftRightArithmetic %int %3181 %int_5
       %3183 = OpIAdd %int %3180 %3182
       %3184 = OpShiftLeftLogical %int %3183 %int_7
       %3186 = OpBitwiseAnd %int %3172 %int_3
       %3187 = OpShiftLeftLogical %int %3186 %int_5
       %3189 = OpShiftRightArithmetic %int %3176 %int_1
       %3190 = OpBitwiseAnd %int %3189 %int_3
       %3191 = OpShiftLeftLogical %int %3190 %int_3
       %3192 = OpBitwiseOr %int %3187 %3191
       %3194 = OpBitwiseAnd %int %3181 %int_7
       %3195 = OpBitwiseOr %int %3192 %3194
       %3198 = OpBitwiseOr %int %3184 %3195
       %3199 = OpShiftLeftLogical %int %3198 %uint_3
       %3201 = OpShiftRightArithmetic %int %3176 %int_3
       %3204 = OpBitwiseXor %int %3201 %3173
       %3205 = OpBitwiseAnd %int %3204 %int_1
       %3207 = OpShiftRightArithmetic %int %3181 %int_3
       %3208 = OpBitwiseAnd %int %3207 %int_3
       %3210 = OpShiftLeftLogical %int %3205 %int_1
       %3211 = OpBitwiseXor %int %3208 %3210
       %3216 = OpBitwiseAnd %int %3176 %int_1
       %3220 = OpShiftLeftLogical %int %3216 %int_4
       %3221 = OpShiftLeftLogical %int %3211 %int_6
       %3222 = OpBitwiseOr %int %3220 %3221
       %3223 = OpShiftLeftLogical %int %3205 %int_11
       %3224 = OpBitwiseOr %int %3222 %3223
       %3225 = OpBitwiseAnd %int %3199 %int_15
       %3226 = OpBitwiseOr %int %3224 %3225
       %3227 = OpShiftRightArithmetic %int %3199 %int_4
       %3228 = OpBitwiseAnd %int %3227 %int_1
       %3229 = OpShiftLeftLogical %int %3228 %int_5
       %3230 = OpBitwiseOr %int %3226 %3229
       %3231 = OpShiftRightArithmetic %int %3199 %int_5
       %3232 = OpBitwiseAnd %int %3231 %int_7
       %3233 = OpShiftLeftLogical %int %3232 %int_8
       %3234 = OpBitwiseOr %int %3230 %3233
       %3235 = OpShiftRightArithmetic %int %3199 %int_8
       %3236 = OpShiftLeftLogical %int %3235 %int_12
       %3237 = OpBitwiseOr %int %3234 %3236
       %3071 = OpBitcast %uint %3237
               OpBranch %3078
       %3078 = OpLabel
       %3688 = OpPhi %uint %3071 %3061 %3077 %3072
       %3082 = OpIMul %uint %1516 %1932
       %3083 = OpIMul %uint %3688 %3082
       %3086 = OpIAdd %uint %3083 %3145
       %1423 = OpShiftRightLogical %uint %3086 %int_4
       %3306 = OpIEqual %bool %1524 %uint_4
               OpSelectionMerge %3310 None
               OpBranchConditional %3306 %3307 %3310
       %3307 = OpLabel
       %3309 = OpVectorShuffle %v4uint %3683 %3683 1 0 3 2
               OpBranch %3310
       %3310 = OpLabel
       %3691 = OpPhi %v4uint %3683 %3078 %3309 %3307
       %3725 = OpSelect %uint %3306 %uint_2 %1524
       %3317 = OpIEqual %bool %3725 %uint_1
       %3319 = OpIEqual %bool %3725 %uint_2
       %3320 = OpLogicalOr %bool %3317 %3319
               OpSelectionMerge %3333 None
               OpBranchConditional %3320 %3321 %3333
       %3321 = OpLabel
       %3324 = OpBitwiseAnd %v4uint %3691 %3716
       %3326 = OpShiftLeftLogical %v4uint %3324 %3717
       %3329 = OpBitwiseAnd %v4uint %3691 %3718
       %3331 = OpShiftRightLogical %v4uint %3329 %3717
       %3332 = OpBitwiseOr %v4uint %3326 %3331
               OpBranch %3333
       %3333 = OpLabel
       %3693 = OpPhi %v4uint %3691 %3310 %3332 %3321
       %3337 = OpIEqual %bool %3725 %uint_3
       %3338 = OpLogicalOr %bool %3319 %3337
               OpSelectionMerge %3347 None
               OpBranchConditional %3338 %3339 %3347
       %3339 = OpLabel
       %3342 = OpShiftLeftLogical %v4uint %3693 %3719
       %3345 = OpShiftRightLogical %v4uint %3693 %3719
       %3346 = OpBitwiseOr %v4uint %3342 %3345
               OpBranch %3347
       %3347 = OpLabel
       %3694 = OpPhi %v4uint %3693 %3333 %3346 %3339
       %1428 = OpAccessChain %_ptr_StorageBuffer_v4uint %xe_resolve_dest %int_0 %1423
               OpStore %1428 %3694
       %1431 = OpIAdd %uint %3086 %uint_16
       %1433 = OpShiftRightLogical %uint %1431 %int_4
               OpSelectionMerge %3361 None
               OpBranchConditional %3306 %3358 %3361
       %3358 = OpLabel
       %3360 = OpVectorShuffle %v4uint %3685 %3685 1 0 3 2
               OpBranch %3361
       %3361 = OpLabel
       %3703 = OpPhi %v4uint %3685 %3347 %3360 %3358
               OpSelectionMerge %3384 None
               OpBranchConditional %3320 %3372 %3384
       %3372 = OpLabel
       %3375 = OpBitwiseAnd %v4uint %3703 %3716
       %3377 = OpShiftLeftLogical %v4uint %3375 %3717
       %3380 = OpBitwiseAnd %v4uint %3703 %3718
       %3382 = OpShiftRightLogical %v4uint %3380 %3717
       %3383 = OpBitwiseOr %v4uint %3377 %3382
               OpBranch %3384
       %3384 = OpLabel
       %3705 = OpPhi %v4uint %3703 %3361 %3383 %3372
               OpSelectionMerge %3398 None
               OpBranchConditional %3338 %3390 %3398
       %3390 = OpLabel
       %3393 = OpShiftLeftLogical %v4uint %3705 %3719
       %3396 = OpShiftRightLogical %v4uint %3705 %3719
       %3397 = OpBitwiseOr %v4uint %3393 %3396
               OpBranch %3398
       %3398 = OpLabel
       %3706 = OpPhi %v4uint %3705 %3384 %3397 %3390
       %1438 = OpAccessChain %_ptr_StorageBuffer_v4uint %xe_resolve_dest %int_0 %1433
               OpStore %1438 %3706
               OpBranch %1439
       %1439 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t resolve_host_color_64bpp_2xmsaa_scaled_cs[] = {
    0x07230203, 0x00010300, 0x000D000A, 0x00000E8E, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x00000004, 0x6E69616D, 0x00000000, 0x000004F8, 0x00060010, 0x00000004,
    0x00000011, 0x00000008, 0x00000008, 0x00000001, 0x00030003, 0x00000002,
    0x000001CC, 0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F,
    0x5F776F6C, 0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47,
    0x735F5458, 0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572,
    0x74636E75, 0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6E69616D,
    0x00000000, 0x00070005, 0x000001A8, 0x68737570, 0x6E6F635F, 0x625F7473,
    0x6B636F6C, 0x0065785F, 0x00090006, 0x000001A8, 0x00000000, 0x725F6578,
    0x6C6F7365, 0x655F6576, 0x6D617264, 0x666E695F, 0x0000006F, 0x000A0006,
    0x000001A8, 0x00000001, 0x725F6578, 0x6C6F7365, 0x635F6576, 0x64726F6F,
    0x74616E69, 0x6E695F65, 0x00006F66, 0x00090006, 0x000001A8, 0x00000002,
    0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365, 0x6F666E69, 0x00000000,
    0x000B0006, 0x000001A8, 0x00000003, 0x725F6578, 0x6C6F7365, 0x645F6576,
    0x5F747365, 0x726F6F63, 0x616E6964, 0x695F6574, 0x006F666E, 0x00060005,
    0x000001AA, 0x68737570, 0x6E6F635F, 0x5F737473, 0x00006578, 0x000A0005,
    0x0000032E, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x65785F72, 0x6F6C625F, 0x00006B63, 0x000D0006, 0x0000032E, 0x00000000,
    0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x69645F72,
    0x74617073, 0x6F5F6863, 0x65736666, 0x00000074, 0x000B0006, 0x0000032E,
    0x00000001, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x75645F72, 0x625F706D, 0x00657361, 0x000D0006, 0x0000032E, 0x00000002,
    0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x75645F72,
    0x705F706D, 0x68637469, 0x6C69745F, 0x00007365, 0x000D0006, 0x0000032E,
    0x00000003, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x6F735F72, 0x65637275, 0x7361625F, 0x69745F65, 0x0073656C, 0x000E0006,
    0x0000032E, 0x00000004, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F,
    0x6F6C6F63, 0x6F735F72, 0x65637275, 0x7469705F, 0x745F6863, 0x73656C69,
    0x00000000, 0x000D0006, 0x0000032E, 0x00000005, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x68745F72, 0x64616572, 0x756F635F,
    0x785F746E, 0x00000000, 0x000D0006, 0x0000032E, 0x00000006, 0x725F6578,
    0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x68745F72, 0x64616572,
    0x756F635F, 0x795F746E, 0x00000000, 0x000C0006, 0x0000032E, 0x00000007,
    0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x65685F72,
    0x74686769, 0x6163735F, 0x0064656C, 0x000D0006, 0x0000032E, 0x00000008,
    0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x736D5F72,
    0x325F6161, 0x61735F78, 0x656C706D, 0x0000305F, 0x000D0006, 0x0000032E,
    0x00000009, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x736D5F72, 0x325F6161, 0x61735F78, 0x656C706D, 0x0000315F, 0x000A0006,
    0x0000032E, 0x0000000A, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F,
    0x6F6C6F63, 0x6C665F72, 0x00736761, 0x00080005, 0x00000330, 0x725F6578,
    0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x00000072, 0x000A0005,
    0x0000037F, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x6F735F72, 0x65637275, 0x00000000, 0x00090005, 0x000004DE, 0x725F6578,
    0x6C6F7365, 0x645F6576, 0x5F747365, 0x625F6578, 0x6B636F6C, 0x00000000,
    0x00050006, 0x000004DE, 0x00000000, 0x61746164, 0x00000000, 0x00060005,
    0x000004E0, 0x725F6578, 0x6C6F7365, 0x645F6576, 0x00747365, 0x00080005,
    0x000004F8, 0x475F6C67, 0x61626F6C, 0x766E496C, 0x7461636F, 0x496E6F69,
    0x00000044, 0x00050048, 0x000001A8, 0x00000000, 0x00000023, 0x00000000,
    0x00050048, 0x000001A8, 0x00000001, 0x00000023, 0x00000004, 0x00050048,
    0x000001A8, 0x00000002, 0x00000023, 0x00000008, 0x00050048, 0x000001A8,
    0x00000003, 0x00000023, 0x0000000C, 0x00030047, 0x000001A8, 0x00000002,
    0x00050048, 0x0000032E, 0x00000000, 0x00000023, 0x00000000, 0x00050048,
    0x0000032E, 0x00000001, 0x00000023, 0x00000004, 0x00050048, 0x0000032E,
    0x00000002, 0x00000023, 0x00000008, 0x00050048, 0x0000032E, 0x00000003,
    0x00000023, 0x0000000C, 0x00050048, 0x0000032E, 0x00000004, 0x00000023,
    0x00000010, 0x00050048, 0x0000032E, 0x00000005, 0x00000023, 0x00000014,
    0x00050048, 0x0000032E, 0x00000006, 0x00000023, 0x00000018, 0x00050048,
    0x0000032E, 0x00000007, 0x00000023, 0x0000001C, 0x00050048, 0x0000032E,
    0x00000008, 0x00000023, 0x00000020, 0x00050048, 0x0000032E, 0x00000009,
    0x00000023, 0x00000024, 0x00050048, 0x0000032E, 0x0000000A, 0x00000023,
    0x00000028, 0x00030047, 0x0000032E, 0x00000002, 0x00040047, 0x00000330,
    0x00000022, 0x00000000, 0x00040047, 0x00000330, 0x00000021, 0x00000001,
    0x00040047, 0x0000037F, 0x00000022, 0x00000002, 0x00040047, 0x0000037F,
    0x00000021, 0x00000000, 0x00040047, 0x000004DD, 0x00000006, 0x00000010,
    0x00040048, 0x000004DE, 0x00000000, 0x00000019, 0x00050048, 0x000004DE,
    0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x000004DE, 0x00000002,
    0x00040047, 0x000004E0, 0x00000022, 0x00000001, 0x00040047, 0x000004E0,
    0x00000021, 0x00000000, 0x00040047, 0x000004F8, 0x0000000B, 0x0000001C,
    0x00040047, 0x000004FD, 0x0000000B, 0x00000019, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020,
    0x00000000, 0x00040017, 0x00000008, 0x00000006, 0x00000002, 0x00040017,
    0x0000000D, 0x00000006, 0x00000004, 0x00040015, 0x00000018, 0x00000020,
    0x00000001, 0x00040017, 0x00000020, 0x00000018, 0x00000002, 0x00040017,
    0x00000027, 0x00000018, 0x00000003, 0x00020014, 0x0000003A, 0x00030016,
    0x0000003B, 0x00000020, 0x00040017, 0x0000006C, 0x0000003B, 0x00000004,
    0x0004002B, 0x00000006, 0x000000A7, 0x00000001, 0x0004002B, 0x00000006,
    0x000000AA, 0x00000002, 0x0004002B, 0x00000006, 0x000000B0, 0x00FF00FF,
    0x0004002B, 0x00000006, 0x000000B3, 0x00000008, 0x0004002B, 0x00000006,
    0x000000B7, 0xFF00FF00, 0x0004002B, 0x00000006, 0x000000C0, 0x00000003,
    0x0004002B, 0x00000006, 0x000000C6, 0x00000010, 0x0004002B, 0x00000006,
    0x000000D1, 0x00000004, 0x0004002B, 0x00000018, 0x000000DE, 0x00000004,
    0x0004002B, 0x00000018, 0x000000E0, 0x00000006, 0x0004002B, 0x00000018,
    0x000000E3, 0x0000000B, 0x0004002B, 0x00000018, 0x000000E6, 0x0000000F,
    0x0004002B, 0x00000018, 0x000000EA, 0x00000001, 0x0004002B, 0x00000018,
    0x000000EC, 0x00000005, 0x0004002B, 0x00000018, 0x000000F0, 0x00000007,
    0x0004002B, 0x00000018, 0x000000F2, 0x00000008, 0x0004002B, 0x00000018,
    0x000000F6, 0x0000000C, 0x0004002B, 0x00000006, 0x00000101, 0x00000000,
    0x0004002B, 0x00000018, 0x0000010A, 0x00000003, 0x0004002B, 0x00000018,
    0x0000012B, 0x00000002, 0x0004002B, 0x00000006, 0x00000163, 0x00000005,
    0x0004002B, 0x00000018, 0x0000017B, 0x00000000, 0x0006001E, 0x000001A8,
    0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00040020, 0x000001A9,
    0x00000009, 0x000001A8, 0x0004003B, 0x000001A9, 0x000001AA, 0x00000009,
    0x00040020, 0x000001AB, 0x00000009, 0x00000006, 0x0004002B, 0x00000006,
    0x000001B3, 0x000003FF, 0x0004002B, 0x00000006, 0x000001B7, 0x0000000A,
    0x0004002B, 0x00000006, 0x000001C4, 0x000007FF, 0x0004002B, 0x00000006,
    0x000001C8, 0x00000018, 0x0004002B, 0x00000006, 0x000001CA, 0x0000000F,
    0x0004002B, 0x00000006, 0x000001CE, 0x0000001C, 0x0004002B, 0x00000006,
    0x000001D5, 0x00000013, 0x0005002C, 0x00000008, 0x000001D6, 0x000000C6,
    0x000001D5, 0x0004002B, 0x00000006, 0x000001D8, 0x00000007, 0x0004002B,
    0x00000006, 0x000001DD, 0x20000000, 0x0005002C, 0x00000008, 0x000001EE,
    0x00000101, 0x000000D1, 0x0005002C, 0x00000008, 0x000001F2, 0x000000D1,
    0x000000A7, 0x0004002B, 0x00000018, 0x000001FD, 0x00000009, 0x0004002B,
    0x0000003B, 0x00000225, 0x3F800000, 0x0004002B, 0x00000006, 0x0000022B,
    0x01000000, 0x0004002B, 0x00000006, 0x0000023D, 0x00000014, 0x0005002C,
    0x00000008, 0x0000023E, 0x0000023D, 0x000001C8, 0x00040017, 0x00000263,
    0x00000006, 0x00000003, 0x0004002B, 0x00000006, 0x000002A3, 0xFFFF0000,
    0x0004002B, 0x00000006, 0x000002A8, 0x0000FFFF, 0x0004002B, 0x00000006,
    0x000002B4, 0x00000028, 0x0004002B, 0x0000003B, 0x000002CE, 0xBF800000,
    0x0004002B, 0x0000003B, 0x000002D3, 0x00000000, 0x0004002B, 0x0000003B,
    0x000002D5, 0x3F000000, 0x0004002B, 0x0000003B, 0x000002D6, 0xBF000000,
    0x0004002B, 0x0000003B, 0x000002D9, 0x46FFFE00, 0x00040017, 0x000002FD,
    0x0000003B, 0x00000002, 0x000D001E, 0x0000032E, 0x00000006, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00040020, 0x0000032F, 0x00000002,
    0x0000032E, 0x0004003B, 0x0000032F, 0x00000330, 0x00000002, 0x00040020,
    0x00000331, 0x00000002, 0x00000006, 0x0005002C, 0x00000008, 0x0000035E,
    0x000000A7, 0x00000101, 0x00090019, 0x0000037D, 0x0000003B, 0x00000001,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000000, 0x00040020,
    0x0000037E, 0x00000000, 0x0000037D, 0x0004003B, 0x0000037E, 0x0000037F,
    0x00000000, 0x0003002A, 0x0000003A, 0x000003A1, 0x00030029, 0x0000003A,
    0x00000413, 0x0005002C, 0x00000008, 0x00000425, 0x00000101, 0x000000A7,
    0x0003001D, 0x000004DD, 0x0000000D, 0x0003001E, 0x000004DE, 0x000004DD,
    0x00040020, 0x000004DF, 0x0000000C, 0x000004DE, 0x0004003B, 0x000004DF,
    0x000004E0, 0x0000000C, 0x00040020, 0x000004E9, 0x0000000C, 0x0000000D,
    0x00040020, 0x000004F7, 0x00000001, 0x00000263, 0x0004003B, 0x000004F7,
    0x000004F8, 0x00000001, 0x0006002C, 0x00000263, 0x000004FD, 0x000000B3,
    0x000000B3, 0x000000A7, 0x00030001, 0x00000008, 0x00000E3E, 0x0005002C,
    0x00000008, 0x00000E7B, 0x000001D8, 0x000001D8, 0x0005002C, 0x00000008,
    0x00000E7C, 0x000000A7, 0x000000A7, 0x0005002C, 0x00000008, 0x00000E7D,
    0x00000101, 0x00000101, 0x0005002C, 0x00000008, 0x00000E7E, 0x000000C0,
    0x000000C0, 0x0005002C, 0x00000008, 0x00000E7F, 0x000001CA, 0x000001CA,
    0x0007002C, 0x0000000D, 0x00000E80, 0x000002A3, 0x000002A3, 0x000002A3,
    0x000002A3, 0x0007002C, 0x0000000D, 0x00000E81, 0x000002A8, 0x000002A8,
    0x000002A8, 0x000002A8, 0x0005002C, 0x00000008, 0x00000E82, 0x000000AA,
    0x000000A7, 0x0004002B, 0x00000006, 0x00000E83, 0x00000006, 0x0007002C,
    0x0000000D, 0x00000E84, 0x000000B0, 0x000000B0, 0x000000B0, 0x000000B0,
    0x0007002C, 0x0000000D, 0x00000E85, 0x000000B3, 0x000000B3, 0x000000B3,
    0x000000B3, 0x0007002C, 0x0000000D, 0x00000E86, 0x000000B7, 0x000000B7,
    0x000000B7, 0x000000B7, 0x0007002C, 0x0000000D, 0x00000E87, 0x000000C6,
    0x000000C6, 0x000000C6, 0x000000C6, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0004003D, 0x00000263,
    0x000004FA, 0x000004F8, 0x000300F7, 0x0000059F, 0x00000000, 0x000300FB,
    0x00000101, 0x00000543, 0x000200F8, 0x00000543, 0x00050041, 0x000001AB,
    0x000005AC, 0x000001AA, 0x0000017B, 0x0004003D, 0x00000006, 0x000005AD,
    0x000005AC, 0x00050041, 0x000001AB, 0x000005AE, 0x000001AA, 0x000000EA,
    0x0004003D, 0x00000006, 0x000005AF, 0x000005AE, 0x000500C2, 0x00000006,
    0x000005C0, 0x000005AD, 0x000001C8, 0x000500C7, 0x00000006, 0x000005C1,
    0x000005C0, 0x000001CA, 0x00050050, 0x00000008, 0x0000062C, 0x000005AF,
    0x000005AF, 0x000500C2, 0x00000008, 0x000005C9, 0x0000062C, 0x000001D6,
    0x000500C7, 0x00000008, 0x000005CB, 0x000005C9, 0x00000E7B, 0x000500C7,
    0x00000006, 0x000005CE, 0x000005AD, 0x000001DD, 0x000500AB, 0x0000003A,
    0x000005CF, 0x000005CE, 0x00000101, 0x000300F7, 0x000005D9, 0x00000000,
    0x000400FA, 0x000005CF, 0x000005D0, 0x000005D6, 0x000200F8, 0x000005D6,
    0x000200F9, 0x000005D9, 0x000200F8, 0x000005D0, 0x000500C2, 0x00000008,
    0x000005D4, 0x000005CB, 0x00000E7C, 0x000200F9, 0x000005D9, 0x000200F8,
    0x000005D9, 0x000700F5, 0x00000008, 0x00000E39, 0x000005D4, 0x000005D0,
    0x00000E7D, 0x000005D6, 0x000500C2, 0x00000008, 0x000005DC, 0x0000062C,
    0x000001EE, 0x000500C4, 0x00000008, 0x000005DE, 0x00000E7C, 0x000001F2,
    0x00050082, 0x00000008, 0x000005E0, 0x000005DE, 0x00000E7C, 0x000500C7,
    0x00000008, 0x000005E1, 0x000005DC, 0x000005E0, 0x000500C4, 0x00000008,
    0x000005E3, 0x000005E1, 0x00000E7E, 0x00050084, 0x00000008, 0x000005E6,
    0x000005E3, 0x000005CB, 0x000500C2, 0x00000006, 0x000005E9, 0x000005AF,
    0x00000163, 0x000500C7, 0x00000006, 0x000005EA, 0x000005E9, 0x000001C4,
    0x00050051, 0x00000006, 0x000005EC, 0x000005CB, 0x00000000, 0x00050084,
    0x00000006, 0x000005ED, 0x000005EA, 0x000005EC, 0x00050041, 0x000001AB,
    0x000005EF, 0x000001AA, 0x0000012B, 0x0004003D, 0x00000006, 0x000005F0,
    0x000005EF, 0x00050041, 0x000001AB, 0x000005F1, 0x000001AA, 0x0000010A,
    0x0004003D, 0x00000006, 0x000005F2, 0x000005F1, 0x000500C7, 0x00000006,
    0x000005F4, 0x000005F0, 0x000001D8, 0x000500C7, 0x00000006, 0x000005F7,
    0x000005F0, 0x000000B3, 0x000500AB, 0x0000003A, 0x000005F8, 0x000005F7,
    0x00000101, 0x000500C2, 0x00000006, 0x000005FB, 0x000005F0, 0x000000D1,
    0x000500C7, 0x00000006, 0x000005FC, 0x000005FB, 0x000001D8, 0x000500C7,
    0x00000006, 0x0000060C, 0x000005F0, 0x0000022B, 0x000500AB, 0x0000003A,
    0x0000060D, 0x0000060C, 0x00000101, 0x000500C7, 0x00000006, 0x00000610,
    0x000005F2, 0x000001B3, 0x000500C2, 0x00000006, 0x00000613, 0x000005F2,
    0x000001B7, 0x000500C7, 0x00000006, 0x00000614, 0x00000613, 0x000001B3,
    0x000500C4, 0x00000006, 0x00000615, 0x00000614, 0x000000EA, 0x00050050,
    0x00000008, 0x00000640, 0x000005F2, 0x000005F2, 0x000500C2, 0x00000008,
    0x00000619, 0x00000640, 0x0000023E, 0x000500C7, 0x00000008, 0x0000061B,
    0x00000619, 0x00000E7F, 0x000500C4, 0x00000008, 0x0000061D, 0x0000061B,
    0x00000E7E, 0x00050084, 0x00000008, 0x00000620, 0x0000061D, 0x000005CB,
    0x000500C2, 0x00000006, 0x00000623, 0x000005F2, 0x000001CE, 0x000500C7,
    0x00000006, 0x00000624, 0x00000623, 0x000001D8, 0x000300F7, 0x000006C4,
    0x00000000, 0x000300FB, 0x00000101, 0x00000655, 0x000200F8, 0x00000655,
    0x00050051, 0x00000006, 0x00000657, 0x000004FA, 0x00000000, 0x00050041,
    0x00000331, 0x00000658, 0x00000330, 0x000000EC, 0x0004003D, 0x00000006,
    0x00000659, 0x00000658, 0x000500AE, 0x0000003A, 0x0000065A, 0x00000657,
    0x00000659, 0x000400A8, 0x0000003A, 0x0000065B, 0x0000065A, 0x000300F7,
    0x00000662, 0x00000000, 0x000400FA, 0x0000065B, 0x0000065C, 0x00000662,
    0x000200F8, 0x0000065C, 0x00050051, 0x00000006, 0x0000065E, 0x000004FA,
    0x00000001, 0x00050041, 0x00000331, 0x0000065F, 0x00000330, 0x000000E0,
    0x0004003D, 0x00000006, 0x00000660, 0x0000065F, 0x000500AE, 0x0000003A,
    0x00000661, 0x0000065E, 0x00000660, 0x000200F9, 0x00000662, 0x000200F8,
    0x00000662, 0x000700F5, 0x0000003A, 0x00000663, 0x0000065A, 0x00000655,
    0x00000661, 0x0000065C, 0x000300F7, 0x00000665, 0x00000000, 0x000400FA,
    0x00000663, 0x00000664, 0x00000665, 0x000200F8, 0x00000664, 0x000200F9,
    0x000006C4, 0x000200F8, 0x00000665, 0x00050084, 0x00000006, 0x000006D1,
    0x000002B4, 0x000005EC, 0x00050051, 0x00000006, 0x000006DB, 0x000005CB,
    0x00000001, 0x00050084, 0x00000006, 0x000006DC, 0x000000C6, 0x000006DB,
    0x000500C2, 0x00000006, 0x000006D7, 0x000006DC, 0x000000A7, 0x00050084,
    0x00000006, 0x0000066E, 0x00000657, 0x000000D1, 0x00050051, 0x00000006,
    0x00000670, 0x000004FA, 0x00000001, 0x00050086, 0x00000006, 0x00000673,
    0x0000066E, 0x000006D1, 0x00050086, 0x00000006, 0x00000676, 0x00000670,
    0x000006D7, 0x00050084, 0x00000006, 0x0000067A, 0x00000673, 0x000006D1,
    0x00050082, 0x00000006, 0x0000067B, 0x0000066E, 0x0000067A, 0x00050084,
    0x00000006, 0x0000067F, 0x00000676, 0x000006D7, 0x00050082, 0x00000006,
    0x00000680, 0x00000670, 0x0000067F, 0x00050041, 0x00000331, 0x00000681,
    0x00000330, 0x0000017B, 0x0004003D, 0x00000006, 0x00000682, 0x00000681,
    0x00050041, 0x00000331, 0x00000684, 0x00000330, 0x0000012B, 0x0004003D,
    0x00000006, 0x00000685, 0x00000684, 0x00050084, 0x00000006, 0x00000686,
    0x00000676, 0x00000685, 0x00050080, 0x00000006, 0x00000687, 0x00000682,
    0x00000686, 0x00050080, 0x00000006, 0x00000689, 0x00000687, 0x00000673,
    0x00050086, 0x00000006, 0x0000068E, 0x00000689, 0x00000685, 0x00050084,
    0x00000006, 0x00000692, 0x0000068E, 0x00000685, 0x00050082, 0x00000006,
    0x00000693, 0x00000689, 0x00000692, 0x00050084, 0x00000006, 0x00000696,
    0x00000693, 0x000006D1, 0x00050080, 0x00000006, 0x00000698, 0x00000696,
    0x0000067B, 0x00050084, 0x00000006, 0x0000069B, 0x0000068E, 0x000006D7,
    0x00050080, 0x00000006, 0x0000069D, 0x0000069B, 0x00000680, 0x00050050,
    0x00000008, 0x0000069E, 0x00000698, 0x0000069D, 0x00050051, 0x00000006,
    0x000006A2, 0x000005E6, 0x00000000, 0x000500B0, 0x0000003A, 0x000006A3,
    0x00000698, 0x000006A2, 0x000400A8, 0x0000003A, 0x000006A4, 0x000006A3,
    0x000300F7, 0x000006AB, 0x00000000, 0x000400FA, 0x000006A4, 0x000006A5,
    0x000006AB, 0x000200F8, 0x000006A5, 0x00050051, 0x00000006, 0x000006A9,
    0x000005E6, 0x00000001, 0x000500B0, 0x0000003A, 0x000006AA, 0x0000069D,
    0x000006A9, 0x000200F9, 0x000006AB, 0x000200F8, 0x000006AB, 0x000700F5,
    0x0000003A, 0x000006AC, 0x000006A3, 0x00000665, 0x000006AA, 0x000006A5,
    0x000300F7, 0x000006AE, 0x00000000, 0x000400FA, 0x000006AC, 0x000006AD,
    0x000006AE, 0x000200F8, 0x000006AD, 0x000200F9, 0x000006C4, 0x000200F8,
    0x000006AE, 0x00050082, 0x00000008, 0x000006B2, 0x0000069E, 0x000005E6,
    0x00050051, 0x00000006, 0x000006B4, 0x000006B2, 0x00000000, 0x000500C4,
    0x00000006, 0x000006B7, 0x000005ED, 0x000000C0, 0x000500AE, 0x0000003A,
    0x000006B8, 0x000006B4, 0x000006B7, 0x000400A8, 0x0000003A, 0x000006B9,
    0x000006B8, 0x000300F7, 0x000006C0, 0x00000000, 0x000400FA, 0x000006B9,
    0x000006BA, 0x000006C0, 0x000200F8, 0x000006BA, 0x00050051, 0x00000006,
    0x000006BC, 0x000006B2, 0x00000001, 0x00050041, 0x00000331, 0x000006BD,
    0x00000330, 0x000000F0, 0x0004003D, 0x00000006, 0x000006BE, 0x000006BD,
    0x000500AE, 0x0000003A, 0x000006BF, 0x000006BC, 0x000006BE, 0x000200F9,
    0x000006C0, 0x000200F8, 0x000006C0, 0x000700F5, 0x0000003A, 0x000006C1,
    0x000006B8, 0x000006AE, 0x000006BF, 0x000006BA, 0x000300F7, 0x000006C3,
    0x00000000, 0x000400FA, 0x000006C1, 0x000006C2, 0x000006C3, 0x000200F8,
    0x000006C2, 0x000200F9, 0x000006C4, 0x000200F8, 0x000006C3, 0x000200F9,
    0x000006C4, 0x000200F8, 0x000006C4, 0x000B00F5, 0x00000008, 0x00000E3B,
    0x00000E3E, 0x00000664, 0x00000E3E, 0x000006AD, 0x000006B2, 0x000006C2,
    0x000006B2, 0x000006C3, 0x000B00F5, 0x0000003A, 0x00000E3A, 0x000003A1,
    0x00000664, 0x000003A1, 0x000006AD, 0x000003A1, 0x000006C2, 0x00000413,
    0x000006C3, 0x000400A8, 0x0000003A, 0x00000549, 0x00000E3A, 0x000300F7,
    0x0000054B, 0x00000000, 0x000400FA, 0x00000549, 0x0000054A, 0x0000054B,
    0x000200F8, 0x0000054A, 0x000200F9, 0x0000059F, 0x000200F8, 0x0000054B,
    0x000500B2, 0x0000003A, 0x000006EF, 0x00000624, 0x000000C0, 0x000300F7,
    0x000006F8, 0x00000000, 0x000400FA, 0x000006EF, 0x000006F0, 0x000006F2,
    0x000200F8, 0x000006F2, 0x000500AA, 0x0000003A, 0x000006F4, 0x00000624,
    0x00000163, 0x000600A9, 0x00000006, 0x00000E89, 0x000006F4, 0x000000AA,
    0x00000101, 0x000200F9, 0x000006F8, 0x000200F8, 0x000006F0, 0x000200F9,
    0x000006F8, 0x000200F8, 0x000006F8, 0x000700F5, 0x00000006, 0x00000E41,
    0x00000624, 0x000006F0, 0x00000E89, 0x000006F2, 0x00050051, 0x00000006,
    0x000006FF, 0x00000E3B, 0x00000000, 0x00050051, 0x00000006, 0x00000703,
    0x00000E3B, 0x00000001, 0x00050051, 0x00000006, 0x00000705, 0x00000E39,
    0x00000001, 0x0007000C, 0x00000006, 0x00000706, 0x00000001, 0x00000029,
    0x00000703, 0x00000705, 0x00050050, 0x00000008, 0x00000707, 0x000006FF,
    0x00000706, 0x00050080, 0x00000008, 0x0000070A, 0x00000707, 0x000005E6,
    0x000500C4, 0x00000008, 0x0000070C, 0x0000070A, 0x00000425, 0x00050050,
    0x00000008, 0x0000071C, 0x00000E41, 0x00000E41, 0x000500C2, 0x00000008,
    0x00000715, 0x0000071C, 0x0000035E, 0x000500C7, 0x00000008, 0x00000717,
    0x00000715, 0x00000E7C, 0x00050080, 0x00000008, 0x0000070F, 0x0000070C,
    0x00000717, 0x00050084, 0x00000006, 0x00000788, 0x000002B4, 0x000005EC,
    0x00050051, 0x00000006, 0x0000078C, 0x000005CB, 0x00000001, 0x00050084,
    0x00000006, 0x0000078D, 0x000000C6, 0x0000078C, 0x00050051, 0x00000006,
    0x0000074E, 0x0000070F, 0x00000000, 0x00050086, 0x00000006, 0x00000750,
    0x0000074E, 0x00000788, 0x00050051, 0x00000006, 0x00000752, 0x0000070F,
    0x00000001, 0x00050086, 0x00000006, 0x00000754, 0x00000752, 0x0000078D,
    0x00050084, 0x00000006, 0x00000759, 0x00000750, 0x00000788, 0x00050082,
    0x00000006, 0x0000075A, 0x0000074E, 0x00000759, 0x00050084, 0x00000006,
    0x0000075F, 0x00000754, 0x0000078D, 0x00050082, 0x00000006, 0x00000760,
    0x00000752, 0x0000075F, 0x00050041, 0x00000331, 0x00000762, 0x00000330,
    0x0000012B, 0x0004003D, 0x00000006, 0x00000763, 0x00000762, 0x00050084,
    0x00000006, 0x00000764, 0x00000754, 0x00000763, 0x00050080, 0x00000006,
    0x00000766, 0x00000764, 0x00000750, 0x00050041, 0x00000331, 0x00000767,
    0x00000330, 0x000000EA, 0x0004003D, 0x00000006, 0x00000768, 0x00000767,
    0x00050080, 0x00000006, 0x0000076A, 0x00000768, 0x00000766, 0x00050041,
    0x00000331, 0x0000076C, 0x00000330, 0x0000010A, 0x0004003D, 0x00000006,
    0x0000076D, 0x0000076C, 0x00050082, 0x00000006, 0x0000076E, 0x0000076A,
    0x0000076D, 0x00050041, 0x00000331, 0x0000076F, 0x00000330, 0x000000DE,
    0x0004003D, 0x00000006, 0x00000770, 0x0000076F, 0x00050086, 0x00000006,
    0x00000773, 0x0000076E, 0x00000770, 0x00050084, 0x00000006, 0x00000777,
    0x00000773, 0x00000770, 0x00050082, 0x00000006, 0x00000778, 0x0000076E,
    0x00000777, 0x00050084, 0x00000006, 0x0000077B, 0x00000778, 0x00000788,
    0x00050080, 0x00000006, 0x0000077D, 0x0000077B, 0x0000075A, 0x00050084,
    0x00000006, 0x00000780, 0x00000773, 0x0000078D, 0x00050080, 0x00000006,
    0x00000782, 0x00000780, 0x00000760, 0x000500C7, 0x00000006, 0x00000792,
    0x00000782, 0x000000A7, 0x000500AB, 0x0000003A, 0x00000793, 0x00000792,
    0x00000101, 0x000300F7, 0x0000079A, 0x00000000, 0x000400FA, 0x00000793,
    0x00000794, 0x00000797, 0x000200F8, 0x00000797, 0x00050041, 0x00000331,
    0x00000798, 0x00000330, 0x000000F2, 0x0004003D, 0x00000006, 0x00000799,
    0x00000798, 0x000200F9, 0x0000079A, 0x000200F8, 0x00000794, 0x00050041,
    0x00000331, 0x00000795, 0x00000330, 0x000001FD, 0x0004003D, 0x00000006,
    0x00000796, 0x00000795, 0x000200F9, 0x0000079A, 0x000200F8, 0x0000079A,
    0x000700F5, 0x00000006, 0x00000E43, 0x00000796, 0x00000794, 0x00000799,
    0x00000797, 0x0004003D, 0x0000037D, 0x0000072A, 0x0000037F, 0x0004007C,
    0x00000018, 0x0000072D, 0x0000077D, 0x000500C2, 0x00000006, 0x00000730,
    0x00000782, 0x000000A7, 0x0004007C, 0x00000018, 0x00000731, 0x00000730,
    0x00050050, 0x00000020, 0x00000735, 0x0000072D, 0x00000731, 0x0004007C,
    0x00000018, 0x00000737, 0x00000E43, 0x0007005F, 0x0000006C, 0x00000738,
    0x0000072A, 0x00000735, 0x00000040, 0x00000737, 0x000300F7, 0x000007C4,
    0x00000000, 0x000700FB, 0x000005C1, 0x000007A6, 0x00000005, 0x000007AA,
    0x00000007, 0x000007BC, 0x000200F8, 0x000007BC, 0x0007004F, 0x000002FD,
    0x000007BE, 0x00000738, 0x00000738, 0x00000000, 0x00000001, 0x0006000C,
    0x00000006, 0x000007BF, 0x00000001, 0x0000003A, 0x000007BE, 0x0007004F,
    0x000002FD, 0x000007C1, 0x00000738, 0x00000738, 0x00000002, 0x00000003,
    0x0006000C, 0x00000006, 0x000007C2, 0x00000001, 0x0000003A, 0x000007C1,
    0x00050050, 0x00000008, 0x000007C3, 0x000007BF, 0x000007C2, 0x000200F9,
    0x000007C4, 0x000200F8, 0x000007AA, 0x00050051, 0x0000003B, 0x000007AC,
    0x00000738, 0x00000000, 0x0007000C, 0x0000003B, 0x000007CE, 0x00000001,
    0x00000028, 0x000007AC, 0x000002CE, 0x0007000C, 0x0000003B, 0x000007CF,
    0x00000001, 0x00000025, 0x000007CE, 0x00000225, 0x000500BE, 0x0000003A,
    0x000007D1, 0x000007CF, 0x000002D3, 0x000600A9, 0x0000003B, 0x000007D2,
    0x000007D1, 0x000002D5, 0x000002D6, 0x0008000C, 0x0000003B, 0x000007D6,
    0x00000001, 0x00000032, 0x000007CF, 0x000002D9, 0x000007D2, 0x0004006E,
    0x00000018, 0x000007D7, 0x000007D6, 0x0004007C, 0x00000006, 0x000007D8,
    0x000007D7, 0x000500C7, 0x00000006, 0x000007D9, 0x000007D8, 0x000002A8,
    0x00050051, 0x0000003B, 0x000007AF, 0x00000738, 0x00000001, 0x0007000C,
    0x0000003B, 0x000007DF, 0x00000001, 0x00000028, 0x000007AF, 0x000002CE,
    0x0007000C, 0x0000003B, 0x000007E0, 0x00000001, 0x00000025, 0x000007DF,
    0x00000225, 0x000500BE, 0x0000003A, 0x000007E2, 0x000007E0, 0x000002D3,
    0x000600A9, 0x0000003B, 0x000007E3, 0x000007E2, 0x000002D5, 0x000002D6,
    0x0008000C, 0x0000003B, 0x000007E7, 0x00000001, 0x00000032, 0x000007E0,
    0x000002D9, 0x000007E3, 0x0004006E, 0x00000018, 0x000007E8, 0x000007E7,
    0x0004007C, 0x00000006, 0x000007E9, 0x000007E8, 0x000500C7, 0x00000006,
    0x000007EA, 0x000007E9, 0x000002A8, 0x000500C4, 0x00000006, 0x000007B1,
    0x000007EA, 0x000000C6, 0x000500C5, 0x00000006, 0x000007B2, 0x000007D9,
    0x000007B1, 0x00050051, 0x0000003B, 0x000007B4, 0x00000738, 0x00000002,
    0x0007000C, 0x0000003B, 0x000007F0, 0x00000001, 0x00000028, 0x000007B4,
    0x000002CE, 0x0007000C, 0x0000003B, 0x000007F1, 0x00000001, 0x00000025,
    0x000007F0, 0x00000225, 0x000500BE, 0x0000003A, 0x000007F3, 0x000007F1,
    0x000002D3, 0x000600A9, 0x0000003B, 0x000007F4, 0x000007F3, 0x000002D5,
    0x000002D6, 0x0008000C, 0x0000003B, 0x000007F8, 0x00000001, 0x00000032,
    0x000007F1, 0x000002D9, 0x000007F4, 0x0004006E, 0x00000018, 0x000007F9,
    0x000007F8, 0x0004007C, 0x00000006, 0x000007FA, 0x000007F9, 0x000500C7,
    0x00000006, 0x000007FB, 0x000007FA, 0x000002A8, 0x00050051, 0x0000003B,
    0x000007B7, 0x00000738, 0x00000003, 0x0007000C, 0x0000003B, 0x00000801,
    0x00000001, 0x00000028, 0x000007B7, 0x000002CE, 0x0007000C, 0x0000003B,
    0x00000802, 0x00000001, 0x00000025, 0x00000801, 0x00000225, 0x000500BE,
    0x0000003A, 0x00000804, 0x00000802, 0x000002D3, 0x000600A9, 0x0000003B,
    0x00000805, 0x00000804, 0x000002D5, 0x000002D6, 0x0008000C, 0x0000003B,
    0x00000809, 0x00000001, 0x00000032, 0x00000802, 0x000002D9, 0x00000805,
    0x0004006E, 0x00000018, 0x0000080A, 0x00000809, 0x0004007C, 0x00000006,
    0x0000080B, 0x0000080A, 0x000500C7, 0x00000006, 0x0000080C, 0x0000080B,
    0x000002A8, 0x000500C4, 0x00000006, 0x000007B9, 0x0000080C, 0x000000C6,
    0x000500C5, 0x00000006, 0x000007BA, 0x000007FB, 0x000007B9, 0x00050050,
    0x00000008, 0x000007BB, 0x000007B2, 0x000007BA, 0x000200F9, 0x000007C4,
    0x000200F8, 0x000007A6, 0x0007004F, 0x000002FD, 0x000007A8, 0x00000738,
    0x00000738, 0x00000000, 0x00000001, 0x0004007C, 0x00000008, 0x000007A9,
    0x000007A8, 0x000200F9, 0x000007C4, 0x000200F8, 0x000007C4, 0x000900F5,
    0x00000008, 0x00000E46, 0x000007A9, 0x000007A6, 0x000007BB, 0x000007AA,
    0x000007C3, 0x000007BC, 0x000300F7, 0x00000828, 0x00000000, 0x000400FA,
    0x000006EF, 0x00000820, 0x00000822, 0x000200F8, 0x00000822, 0x000500AA,
    0x0000003A, 0x00000824, 0x00000624, 0x00000163, 0x000600A9, 0x00000006,
    0x00000E8A, 0x00000824, 0x000000AA, 0x00000101, 0x000200F9, 0x00000828,
    0x000200F8, 0x00000820, 0x000200F9, 0x00000828, 0x000200F8, 0x00000828,
    0x000700F5, 0x00000006, 0x00000E47, 0x00000624, 0x00000820, 0x00000E8A,
    0x00000822, 0x00050080, 0x00000006, 0x00000831, 0x000006FF, 0x000000A7,
    0x00050050, 0x00000008, 0x00000837, 0x00000831, 0x00000706, 0x00050080,
    0x00000008, 0x0000083A, 0x00000837, 0x000005E6, 0x000500C4, 0x00000008,
    0x0000083C, 0x0000083A, 0x00000425, 0x00050050, 0x00000008, 0x0000084C,
    0x00000E47, 0x00000E47, 0x000500C2, 0x00000008, 0x00000845, 0x0000084C,
    0x0000035E, 0x000500C7, 0x00000008, 0x00000847, 0x00000845, 0x00000E7C,
    0x00050080, 0x00000008, 0x0000083F, 0x0000083C, 0x00000847, 0x00050051,
    0x00000006, 0x0000087E, 0x0000083F, 0x00000000, 0x00050086, 0x00000006,
    0x00000880, 0x0000087E, 0x00000788, 0x00050051, 0x00000006, 0x00000882,
    0x0000083F, 0x00000001, 0x00050086, 0x00000006, 0x00000884, 0x00000882,
    0x0000078D, 0x00050084, 0x00000006, 0x00000889, 0x00000880, 0x00000788,
    0x00050082, 0x00000006, 0x0000088A, 0x0000087E, 0x00000889, 0x00050084,
    0x00000006, 0x0000088F, 0x00000884, 0x0000078D, 0x00050082, 0x00000006,
    0x00000890, 0x00000882, 0x0000088F, 0x00050084, 0x00000006, 0x00000894,
    0x00000884, 0x00000763, 0x00050080, 0x00000006, 0x00000896, 0x00000894,
    0x00000880, 0x00050080, 0x00000006, 0x0000089A, 0x00000768, 0x00000896,
    0x00050082, 0x00000006, 0x0000089E, 0x0000089A, 0x0000076D, 0x00050086,
    0x00000006, 0x000008A3, 0x0000089E, 0x00000770, 0x00050084, 0x00000006,
    0x000008A7, 0x000008A3, 0x00000770, 0x00050082, 0x00000006, 0x000008A8,
    0x0000089E, 0x000008A7, 0x00050084, 0x00000006, 0x000008AB, 0x000008A8,
    0x00000788, 0x00050080, 0x00000006, 0x000008AD, 0x000008AB, 0x0000088A,
    0x00050084, 0x00000006, 0x000008B0, 0x000008A3, 0x0000078D, 0x00050080,
    0x00000006, 0x000008B2, 0x000008B0, 0x00000890, 0x000500C7, 0x00000006,
    0x000008C2, 0x000008B2, 0x000000A7, 0x000500AB, 0x0000003A, 0x000008C3,
    0x000008C2, 0x00000101, 0x000300F7, 0x000008CA, 0x00000000, 0x000400FA,
    0x000008C3, 0x000008C4, 0x000008C7, 0x000200F8, 0x000008C7, 0x00050041,
    0x00000331, 0x000008C8, 0x00000330, 0x000000F2, 0x0004003D, 0x00000006,
    0x000008C9, 0x000008C8, 0x000200F9, 0x000008CA, 0x000200F8, 0x000008C4,
    0x00050041, 0x00000331, 0x000008C5, 0x00000330, 0x000001FD, 0x0004003D,
    0x00000006, 0x000008C6, 0x000008C5, 0x000200F9, 0x000008CA, 0x000200F8,
    0x000008CA, 0x000700F5, 0x00000006, 0x00000E49, 0x000008C6, 0x000008C4,
    0x000008C9, 0x000008C7, 0x0004007C, 0x00000018, 0x0000085D, 0x000008AD,
    0x000500C2, 0x00000006, 0x00000860, 0x000008B2, 0x000000A7, 0x0004007C,
    0x00000018, 0x00000861, 0x00000860, 0x00050050, 0x00000020, 0x00000865,
    0x0000085D, 0x00000861, 0x0004007C, 0x00000018, 0x00000867, 0x00000E49,
    0x0007005F, 0x0000006C, 0x00000868, 0x0000072A, 0x00000865, 0x00000040,
    0x00000867, 0x000300F7, 0x000008F4, 0x00000000, 0x000700FB, 0x000005C1,
    0x000008D6, 0x00000005, 0x000008DA, 0x00000007, 0x000008EC, 0x000200F8,
    0x000008EC, 0x0007004F, 0x000002FD, 0x000008EE, 0x00000868, 0x00000868,
    0x00000000, 0x00000001, 0x0006000C, 0x00000006, 0x000008EF, 0x00000001,
    0x0000003A, 0x000008EE, 0x0007004F, 0x000002FD, 0x000008F1, 0x00000868,
    0x00000868, 0x00000002, 0x00000003, 0x0006000C, 0x00000006, 0x000008F2,
    0x00000001, 0x0000003A, 0x000008F1, 0x00050050, 0x00000008, 0x000008F3,
    0x000008EF, 0x000008F2, 0x000200F9, 0x000008F4, 0x000200F8, 0x000008DA,
    0x00050051, 0x0000003B, 0x000008DC, 0x00000868, 0x00000000, 0x0007000C,
    0x0000003B, 0x000008FE, 0x00000001, 0x00000028, 0x000008DC, 0x000002CE,
    0x0007000C, 0x0000003B, 0x000008FF, 0x00000001, 0x00000025, 0x000008FE,
    0x00000225, 0x000500BE, 0x0000003A, 0x00000901, 0x000008FF, 0x000002D3,
    0x000600A9, 0x0000003B, 0x00000902, 0x00000901, 0x000002D5, 0x000002D6,
    0x0008000C, 0x0000003B, 0x00000906, 0x00000001, 0x00000032, 0x000008FF,
    0x000002D9, 0x00000902, 0x0004006E, 0x00000018, 0x00000907, 0x00000906,
    0x0004007C, 0x00000006, 0x00000908, 0x00000907, 0x000500C7, 0x00000006,
    0x00000909, 0x00000908, 0x000002A8, 0x00050051, 0x0000003B, 0x000008DF,
    0x00000868, 0x00000001, 0x0007000C, 0x0000003B, 0x0000090F, 0x00000001,
    0x00000028, 0x000008DF, 0x000002CE, 0x0007000C, 0x0000003B, 0x00000910,
    0x00000001, 0x00000025, 0x0000090F, 0x00000225, 0x000500BE, 0x0000003A,
    0x00000912, 0x00000910, 0x000002D3, 0x000600A9, 0x0000003B, 0x00000913,
    0x00000912, 0x000002D5, 0x000002D6, 0x0008000C, 0x0000003B, 0x00000917,
    0x00000001, 0x00000032, 0x00000910, 0x000002D9, 0x00000913, 0x0004006E,
    0x00000018, 0x00000918, 0x00000917, 0x0004007C, 0x00000006, 0x00000919,
    0x00000918, 0x000500C7, 0x00000006, 0x0000091A, 0x00000919, 0x000002A8,
    0x000500C4, 0x00000006, 0x000008E1, 0x0000091A, 0x000000C6, 0x000500C5,
    0x00000006, 0x000008E2, 0x00000909, 0x000008E1, 0x00050051, 0x0000003B,
    0x000008E4, 0x00000868, 0x00000002, 0x0007000C, 0x0000003B, 0x00000920,
    0x00000001, 0x00000028, 0x000008E4, 0x000002CE, 0x0007000C, 0x0000003B,
    0x00000921, 0x00000001, 0x00000025, 0x00000920, 0x00000225, 0x000500BE,
    0x0000003A, 0x00000923, 0x00000921, 0x000002D3, 0x000600A9, 0x0000003B,
    0x00000924, 0x00000923, 0x000002D5, 0x000002D6, 0x0008000C, 0x0000003B,
    0x00000928, 0x00000001, 0x00000032, 0x00000921, 0x000002D9, 0x00000924,
    0x0004006E, 0x00000018, 0x00000929, 0x00000928, 0x0004007C, 0x00000006,
    0x0000092A, 0x00000929, 0x000500C7, 0x00000006, 0x0000092B, 0x0000092A,
    0x000002A8, 0x00050051, 0x0000003B, 0x000008E7, 0x00000868, 0x00000003,
    0x0007000C, 0x0000003B, 0x00000931, 0x00000001, 0x00000028, 0x000008E7,
    0x000002CE, 0x0007000C, 0x0000003B, 0x00000932, 0x00000001, 0x00000025,
    0x00000931, 0x00000225, 0x000500BE, 0x0000003A, 0x00000934, 0x00000932,
    0x000002D3, 0x000600A9, 0x0000003B, 0x00000935, 0x00000934, 0x000002D5,
    0x000002D6, 0x0008000C, 0x0000003B, 0x00000939, 0x00000001, 0x00000032,
    0x00000932, 0x000002D9, 0x00000935, 0x0004006E, 0x00000018, 0x0000093A,
    0x00000939, 0x0004007C, 0x00000006, 0x0000093B, 0x0000093A, 0x000500C7,
    0x00000006, 0x0000093C, 0x0000093B, 0x000002A8, 0x000500C4, 0x00000006,
    0x000008E9, 0x0000093C, 0x000000C6, 0x000500C5, 0x00000006, 0x000008EA,
    0x0000092B, 0x000008E9, 0x00050050, 0x00000008, 0x000008EB, 0x000008E2,
    0x000008EA, 0x000200F9, 0x000008F4, 0x000200F8, 0x000008D6, 0x0007004F,
    0x000002FD, 0x000008D8, 0x00000868, 0x00000868, 0x00000000, 0x00000001,
    0x0004007C, 0x00000008, 0x000008D9, 0x000008D8, 0x000200F9, 0x000008F4,
    0x000200F8, 0x000008F4, 0x000900F5, 0x00000008, 0x00000E4C, 0x000008D9,
    0x000008D6, 0x000008EB, 0x000008DA, 0x000008F3, 0x000008EC, 0x000300F7,
    0x00000958, 0x00000000, 0x000400FA, 0x000006EF, 0x00000950, 0x00000952,
    0x000200F8, 0x00000952, 0x000500AA, 0x0000003A, 0x00000954, 0x00000624,
    0x00000163, 0x000600A9, 0x00000006, 0x00000E8B, 0x00000954, 0x000000AA,
    0x00000101, 0x000200F9, 0x00000958, 0x000200F8, 0x00000950, 0x000200F9,
    0x00000958, 0x000200F8, 0x00000958, 0x000700F5, 0x00000006, 0x00000E4D,
    0x00000624, 0x00000950, 0x00000E8B, 0x00000952, 0x00050080, 0x00000006,
    0x00000961, 0x000006FF, 0x000000AA, 0x00050050, 0x00000008, 0x00000967,
    0x00000961, 0x00000706, 0x00050080, 0x00000008, 0x0000096A, 0x00000967,
    0x000005E6, 0x000500C4, 0x00000008, 0x0000096C, 0x0000096A, 0x00000425,
    0x00050050, 0x00000008, 0x0000097C, 0x00000E4D, 0x00000E4D, 0x000500C2,
    0x00000008, 0x00000975, 0x0000097C, 0x0000035E, 0x000500C7, 0x00000008,
    0x00000977, 0x00000975, 0x00000E7C, 0x00050080, 0x00000008, 0x0000096F,
    0x0000096C, 0x00000977, 0x00050051, 0x00000006, 0x000009AE, 0x0000096F,
    0x00000000, 0x00050086, 0x00000006, 0x000009B0, 0x000009AE, 0x00000788,
    0x00050051, 0x00000006, 0x000009B2, 0x0000096F, 0x00000001, 0x00050086,
    0x00000006, 0x000009B4, 0x000009B2, 0x0000078D, 0x00050084, 0x00000006,
    0x000009B9, 0x000009B0, 0x00000788, 0x00050082, 0x00000006, 0x000009BA,
    0x000009AE, 0x000009B9, 0x00050084, 0x00000006, 0x000009BF, 0x000009B4,
    0x0000078D, 0x00050082, 0x00000006, 0x000009C0, 0x000009B2, 0x000009BF,
    0x00050084, 0x00000006, 0x000009C4, 0x000009B4, 0x00000763, 0x00050080,
    0x00000006, 0x000009C6, 0x000009C4, 0x000009B0, 0x00050080, 0x00000006,
    0x000009CA, 0x00000768, 0x000009C6, 0x00050082, 0x00000006, 0x000009CE,
    0x000009CA, 0x0000076D, 0x00050086, 0x00000006, 0x000009D3, 0x000009CE,
    0x00000770, 0x00050084, 0x00000006, 0x000009D7, 0x000009D3, 0x00000770,
    0x00050082, 0x00000006, 0x000009D8, 0x000009CE, 0x000009D7, 0x00050084,
    0x00000006, 0x000009DB, 0x000009D8, 0x00000788, 0x00050080, 0x00000006,
    0x000009DD, 0x000009DB, 0x000009BA, 0x00050084, 0x00000006, 0x000009E0,
    0x000009D3, 0x0000078D, 0x00050080, 0x00000006, 0x000009E2, 0x000009E0,
    0x000009C0, 0x000500C7, 0x00000006, 0x000009F2, 0x000009E2, 0x000000A7,
    0x000500AB, 0x0000003A, 0x000009F3, 0x000009F2, 0x00000101, 0x000300F7,
    0x000009FA, 0x00000000, 0x000400FA, 0x000009F3, 0x000009F4, 0x000009F7,
    0x000200F8, 0x000009F7, 0x00050041, 0x00000331, 0x000009F8, 0x00000330,
    0x000000F2, 0x0004003D, 0x00000006, 0x000009F9, 0x000009F8, 0x000200F9,
    0x000009FA, 0x000200F8, 0x000009F4, 0x00050041, 0x00000331, 0x000009F5,
    0x00000330, 0x000001FD, 0x0004003D, 0x00000006, 0x000009F6, 0x000009F5,
    0x000200F9, 0x000009FA, 0x000200F8, 0x000009FA, 0x000700F5, 0x00000006,
    0x00000E4F, 0x000009F6, 0x000009F4, 0x000009F9, 0x000009F7, 0x0004007C,
    0x00000018, 0x0000098D, 0x000009DD, 0x000500C2, 0x00000006, 0x00000990,
    0x000009E2, 0x000000A7, 0x0004007C, 0x00000018, 0x00000991, 0x00000990,
    0x00050050, 0x00000020, 0x00000995, 0x0000098D, 0x00000991, 0x0004007C,
    0x00000018, 0x00000997, 0x00000E4F, 0x0007005F, 0x0000006C, 0x00000998,
    0x0000072A, 0x00000995, 0x00000040, 0x00000997, 0x000300F7, 0x00000A24,
    0x00000000, 0x000700FB, 0x000005C1, 0x00000A06, 0x00000005, 0x00000A0A,
    0x00000007, 0x00000A1C, 0x000200F8, 0x00000A1C, 0x0007004F, 0x000002FD,
    0x00000A1E, 0x00000998, 0x00000998, 0x00000000, 0x00000001, 0x0006000C,
    0x00000006, 0x00000A1F, 0x00000001, 0x0000003A, 0x00000A1E, 0x0007004F,
    0x000002FD, 0x00000A21, 0x00000998, 0x00000998, 0x00000002, 0x00000003,
    0x0006000C, 0x00000006, 0x00000A22, 0x00000001, 0x0000003A, 0x00000A21,
    0x00050050, 0x00000008, 0x00000A23, 0x00000A1F, 0x00000A22, 0x000200F9,
    0x00000A24, 0x000200F8, 0x00000A0A, 0x00050051, 0x0000003B, 0x00000A0C,
    0x00000998, 0x00000000, 0x0007000C, 0x0000003B, 0x00000A2E, 0x00000001,
    0x00000028, 0x00000A0C, 0x000002CE, 0x0007000C, 0x0000003B, 0x00000A2F,
    0x00000001, 0x00000025, 0x00000A2E, 0x00000225, 0x000500BE, 0x0000003A,
    0x00000A31, 0x00000A2F, 0x000002D3, 0x000600A9, 0x0000003B, 0x00000A32,
    0x00000A31, 0x000002D5, 0x000002D6, 0x0008000C, 0x0000003B, 0x00000A36,
    0x00000001, 0x00000032, 0x00000A2F, 0x000002D9, 0x00000A32, 0x0004006E,
    0x00000018, 0x00000A37, 0x00000A36, 0x0004007C, 0x00000006, 0x00000A38,
    0x00000A37, 0x000500C7, 0x00000006, 0x00000A39, 0x00000A38, 0x000002A8,
    0x00050051, 0x0000003B, 0x00000A0F, 0x00000998, 0x00000001, 0x0007000C,
    0x0000003B, 0x00000A3F, 0x00000001, 0x00000028, 0x00000A0F, 0x000002CE,
    0x0007000C, 0x0000003B, 0x00000A40, 0x00000001, 0x00000025, 0x00000A3F,
    0x00000225, 0x000500BE, 0x0000003A, 0x00000A42, 0x00000A40, 0x000002D3,
    0x000600A9, 0x0000003B, 0x00000A43, 0x00000A42, 0x000002D5, 0x000002D6,
    0x0008000C, 0x0000003B, 0x00000A47, 0x00000001, 0x00000032, 0x00000A40,
    0x000002D9, 0x00000A43, 0x0004006E, 0x00000018, 0x00000A48, 0x00000A47,
    0x0004007C, 0x00000006, 0x00000A49, 0x00000A48, 0x000500C7, 0x00000006,
    0x00000A4A, 0x00000A49, 0x000002A8, 0x000500C4, 0x00000006, 0x00000A11,
    0x00000A4A, 0x000000C6, 0x000500C5, 0x00000006, 0x00000A12, 0x00000A39,
    0x00000A11, 0x00050051, 0x0000003B, 0x00000A14, 0x00000998, 0x00000002,
    0x0007000C, 0x0000003B, 0x00000A50, 0x00000001, 0x00000028, 0x00000A14,
    0x000002CE, 0x0007000C, 0x0000003B, 0x00000A51, 0x00000001, 0x00000025,
    0x00000A50, 0x00000225, 0x000500BE, 0x0000003A, 0x00000A53, 0x00000A51,
    0x000002D3, 0x000600A9, 0x0000003B, 0x00000A54, 0x00000A53, 0x000002D5,
    0x000002D6, 0x0008000C, 0x0000003B, 0x00000A58, 0x00000001, 0x00000032,
    0x00000A51, 0x000002D9, 0x00000A54, 0x0004006E, 0x00000018, 0x00000A59,
    0x00000A58, 0x0004007C, 0x00000006, 0x00000A5A, 0x00000A59, 0x000500C7,
    0x00000006, 0x00000A5B, 0x00000A5A, 0x000002A8, 0x00050051, 0x0000003B,
    0x00000A17, 0x00000998, 0x00000003, 0x0007000C, 0x0000003B, 0x00000A61,
    0x00000001, 0x00000028, 0x00000A17, 0x000002CE, 0x0007000C, 0x0000003B,
    0x00000A62, 0x00000001, 0x00000025, 0x00000A61, 0x00000225, 0x000500BE,
    0x0000003A, 0x00000A64, 0x00000A62, 0x000002D3, 0x000600A9, 0x0000003B,
    0x00000A65, 0x00000A64, 0x000002D5, 0x000002D6, 0x0008000C, 0x0000003B,
    0x00000A69, 0x00000001, 0x00000032, 0x00000A62, 0x000002D9, 0x00000A65,
    0x0004006E, 0x00000018, 0x00000A6A, 0x00000A69, 0x0004007C, 0x00000006,
    0x00000A6B, 0x00000A6A, 0x000500C7, 0x00000006, 0x00000A6C, 0x00000A6B,
    0x000002A8, 0x000500C4, 0x00000006, 0x00000A19, 0x00000A6C, 0x000000C6,
    0x000500C5, 0x00000006, 0x00000A1A, 0x00000A5B, 0x00000A19, 0x00050050,
    0x00000008, 0x00000A1B, 0x00000A12, 0x00000A1A, 0x000200F9, 0x00000A24,
    0x000200F8, 0x00000A06, 0x0007004F, 0x000002FD, 0x00000A08, 0x00000998,
    0x00000998, 0x00000000, 0x00000001, 0x0004007C, 0x00000008, 0x00000A09,
    0x00000A08, 0x000200F9, 0x00000A24, 0x000200F8, 0x00000A24, 0x000900F5,
    0x00000008, 0x00000E52, 0x00000A09, 0x00000A06, 0x00000A1B, 0x00000A0A,
    0x00000A23, 0x00000A1C, 0x000300F7, 0x00000A88, 0x00000000, 0x000400FA,
    0x000006EF, 0x00000A80, 0x00000A82, 0x000200F8, 0x00000A82, 0x000500AA,
    0x0000003A, 0x00000A84, 0x00000624, 0x00000163, 0x000600A9, 0x00000006,
    0x00000E8C, 0x00000A84, 0x000000AA, 0x00000101, 0x000200F9, 0x00000A88,
    0x000200F8, 0x00000A80, 0x000200F9, 0x00000A88, 0x000200F8, 0x00000A88,
    0x000700F5, 0x00000006, 0x00000E53, 0x00000624, 0x00000A80, 0x00000E8C,
    0x00000A82, 0x00050080, 0x00000006, 0x00000A91, 0x000006FF, 0x000000C0,
    0x00050050, 0x00000008, 0x00000A97, 0x00000A91, 0x00000706, 0x00050080,
    0x00000008, 0x00000A9A, 0x00000A97, 0x000005E6, 0x000500C4, 0x00000008,
    0x00000A9C, 0x00000A9A, 0x00000425, 0x00050050, 0x00000008, 0x00000AAC,
    0x00000E53, 0x00000E53, 0x000500C2, 0x00000008, 0x00000AA5, 0x00000AAC,
    0x0000035E, 0x000500C7, 0x00000008, 0x00000AA7, 0x00000AA5, 0x00000E7C,
    0x00050080, 0x00000008, 0x00000A9F, 0x00000A9C, 0x00000AA7, 0x00050051,
    0x00000006, 0x00000ADE, 0x00000A9F, 0x00000000, 0x00050086, 0x00000006,
    0x00000AE0, 0x00000ADE, 0x00000788, 0x00050051, 0x00000006, 0x00000AE2,
    0x00000A9F, 0x00000001, 0x00050086, 0x00000006, 0x00000AE4, 0x00000AE2,
    0x0000078D, 0x00050084, 0x00000006, 0x00000AE9, 0x00000AE0, 0x00000788,
    0x00050082, 0x00000006, 0x00000AEA, 0x00000ADE, 0x00000AE9, 0x00050084,
    0x00000006, 0x00000AEF, 0x00000AE4, 0x0000078D, 0x00050082, 0x00000006,
    0x00000AF0, 0x00000AE2, 0x00000AEF, 0x00050084, 0x00000006, 0x00000AF4,
    0x00000AE4, 0x00000763, 0x00050080, 0x00000006, 0x00000AF6, 0x00000AF4,
    0x00000AE0, 0x00050080, 0x00000006, 0x00000AFA, 0x00000768, 0x00000AF6,
    0x00050082, 0x00000006, 0x00000AFE, 0x00000AFA, 0x0000076D, 0x00050086,
    0x00000006, 0x00000B03, 0x00000AFE, 0x00000770, 0x00050084, 0x00000006,
    0x00000B07, 0x00000B03, 0x00000770, 0x00050082, 0x00000006, 0x00000B08,
    0x00000AFE, 0x00000B07, 0x00050084, 0x00000006, 0x00000B0B, 0x00000B08,
    0x00000788, 0x00050080, 0x00000006, 0x00000B0D, 0x00000B0B, 0x00000AEA,
    0x00050084, 0x00000006, 0x00000B10, 0x00000B03, 0x0000078D, 0x00050080,
    0x00000006, 0x00000B12, 0x00000B10, 0x00000AF0, 0x000500C7, 0x00000006,
    0x00000B22, 0x00000B12, 0x000000A7, 0x000500AB, 0x0000003A, 0x00000B23,
    0x00000B22, 0x00000101, 0x000300F7, 0x00000B2A, 0x00000000, 0x000400FA,
    0x00000B23, 0x00000B24, 0x00000B27, 0x000200F8, 0x00000B27, 0x00050041,
    0x00000331, 0x00000B28, 0x00000330, 0x000000F2, 0x0004003D, 0x00000006,
    0x00000B29, 0x00000B28, 0x000200F9, 0x00000B2A, 0x000200F8, 0x00000B24,
    0x00050041, 0x00000331, 0x00000B25, 0x00000330, 0x000001FD, 0x0004003D,
    0x00000006, 0x00000B26, 0x00000B25, 0x000200F9, 0x00000B2A, 0x000200F8,
    0x00000B2A, 0x000700F5, 0x00000006, 0x00000E55, 0x00000B26, 0x00000B24,
    0x00000B29, 0x00000B27, 0x0004007C, 0x00000018, 0x00000ABD, 0x00000B0D,
    0x000500C2, 0x00000006, 0x00000AC0, 0x00000B12, 0x000000A7, 0x0004007C,
    0x00000018, 0x00000AC1, 0x00000AC0, 0x00050050, 0x00000020, 0x00000AC5,
    0x00000ABD, 0x00000AC1, 0x0004007C, 0x00000018, 0x00000AC7, 0x00000E55,
    0x0007005F, 0x0000006C, 0x00000AC8, 0x0000072A, 0x00000AC5, 0x00000040,
    0x00000AC7, 0x000300F7, 0x00000B54, 0x00000000, 0x000700FB, 0x000005C1,
    0x00000B36, 0x00000005, 0x00000B3A, 0x00000007, 0x00000B4C, 0x000200F8,
    0x00000B4C, 0x0007004F, 0x000002FD, 0x00000B4E, 0x00000AC8, 0x00000AC8,
    0x00000000, 0x00000001, 0x0006000C, 0x00000006, 0x00000B4F, 0x00000001,
    0x0000003A, 0x00000B4E, 0x0007004F, 0x000002FD, 0x00000B51, 0x00000AC8,
    0x00000AC8, 0x00000002, 0x00000003, 0x0006000C, 0x00000006, 0x00000B52,
    0x00000001, 0x0000003A, 0x00000B51, 0x00050050, 0x00000008, 0x00000B53,
    0x00000B4F, 0x00000B52, 0x000200F9, 0x00000B54, 0x000200F8, 0x00000B3A,
    0x00050051, 0x0000003B, 0x00000B3C, 0x00000AC8, 0x00000000, 0x0007000C,
    0x0000003B, 0x00000B5E, 0x00000001, 0x00000028, 0x00000B3C, 0x000002CE,
    0x0007000C, 0x0000003B, 0x00000B5F, 0x00000001, 0x00000025, 0x00000B5E,
    0x00000225, 0x000500BE, 0x0000003A, 0x00000B61, 0x00000B5F, 0x000002D3,
    0x000600A9, 0x0000003B, 0x00000B62, 0x00000B61, 0x000002D5, 0x000002D6,
    0x0008000C, 0x0000003B, 0x00000B66, 0x00000001, 0x00000032, 0x00000B5F,
    0x000002D9, 0x00000B62, 0x0004006E, 0x00000018, 0x00000B67, 0x00000B66,
    0x0004007C, 0x00000006, 0x00000B68, 0x00000B67, 0x000500C7, 0x00000006,
    0x00000B69, 0x00000B68, 0x000002A8, 0x00050051, 0x0000003B, 0x00000B3F,
    0x00000AC8, 0x00000001, 0x0007000C, 0x0000003B, 0x00000B6F, 0x00000001,
    0x00000028, 0x00000B3F, 0x000002CE, 0x0007000C, 0x0000003B, 0x00000B70,
    0x00000001, 0x00000025, 0x00000B6F, 0x00000225, 0x000500BE, 0x0000003A,
    0x00000B72, 0x00000B70, 0x000002D3, 0x000600A9, 0x0000003B, 0x00000B73,
    0x00000B72, 0x000002D5, 0x000002D6, 0x0008000C, 0x0000003B, 0x00000B77,
    0x00000001, 0x00000032, 0x00000B70, 0x000002D9, 0x00000B73, 0x0004006E,
    0x00000018, 0x00000B78, 0x00000B77, 0x0004007C, 0x00000006, 0x00000B79,
    0x00000B78, 0x000500C7, 0x00000006, 0x00000B7A, 0x00000B79, 0x000002A8,
    0x000500C4, 0x00000006, 0x00000B41, 0x00000B7A, 0x000000C6, 0x000500C5,
    0x00000006, 0x00000B42, 0x00000B69, 0x00000B41, 0x00050051, 0x0000003B,
    0x00000B44, 0x00000AC8, 0x00000002, 0x0007000C, 0x0000003B, 0x00000B80,
    0x00000001, 0x00000028, 0x00000B44, 0x000002CE, 0x0007000C, 0x0000003B,
    0x00000B81, 0x00000001, 0x00000025, 0x00000B80, 0x00000225, 0x000500BE,
    0x0000003A, 0x00000B83, 0x00000B81, 0x000002D3, 0x000600A9, 0x0000003B,
    0x00000B84, 0x00000B83, 0x000002D5, 0x000002D6, 0x0008000C, 0x0000003B,
    0x00000B88, 0x00000001, 0x00000032, 0x00000B81, 0x000002D9, 0x00000B84,
    0x0004006E, 0x00000018, 0x00000B89, 0x00000B88, 0x0004007C, 0x00000006,
    0x00000B8A, 0x00000B89, 0x000500C7, 0x00000006, 0x00000B8B, 0x00000B8A,
    0x000002A8, 0x00050051, 0x0000003B, 0x00000B47, 0x00000AC8, 0x00000003,
    0x0007000C, 0x0000003B, 0x00000B91, 0x00000001, 0x00000028, 0x00000B47,
    0x000002CE, 0x0007000C, 0x0000003B, 0x00000B92, 0x00000001, 0x00000025,
    0x00000B91, 0x00000225, 0x000500BE, 0x0000003A, 0x00000B94, 0x00000B92,
    0x000002D3, 0x000600A9, 0x0000003B, 0x00000B95, 0x00000B94, 0x000002D5,
    0x000002D6, 0x0008000C, 0x0000003B, 0x00000B99, 0x00000001, 0x00000032,
    0x00000B92, 0x000002D9, 0x00000B95, 0x0004006E, 0x00000018, 0x00000B9A,
    0x00000B99, 0x0004007C, 0x00000006, 0x00000B9B, 0x00000B9A, 0x000500C7,
    0x00000006, 0x00000B9C, 0x00000B9B, 0x000002A8, 0x000500C4, 0x00000006,
    0x00000B49, 0x00000B9C, 0x000000C6, 0x000500C5, 0x00000006, 0x00000B4A,
    0x00000B8B, 0x00000B49, 0x00050050, 0x00000008, 0x00000B4B, 0x00000B42,
    0x00000B4A, 0x000200F9, 0x00000B54, 0x000200F8, 0x00000B36, 0x0007004F,
    0x000002FD, 0x00000B38, 0x00000AC8, 0x00000AC8, 0x00000000, 0x00000001,
    0x0004007C, 0x00000008, 0x00000B39, 0x00000B38, 0x000200F9, 0x00000B54,
    0x000200F8, 0x00000B54, 0x000900F5, 0x00000008, 0x00000E58, 0x00000B39,
    0x00000B36, 0x00000B4B, 0x00000B3A, 0x00000B53, 0x00000B4C, 0x00050051,
    0x00000006, 0x0000056D, 0x00000E46, 0x00000000, 0x00050051, 0x00000006,
    0x0000056F, 0x00000E46, 0x00000001, 0x00050051, 0x00000006, 0x00000571,
    0x00000E4C, 0x00000000, 0x00050051, 0x00000006, 0x00000573, 0x00000E4C,
    0x00000001, 0x00070050, 0x0000000D, 0x00000574, 0x0000056D, 0x0000056F,
    0x00000571, 0x00000573, 0x00050051, 0x00000006, 0x00000576, 0x00000E52,
    0x00000000, 0x00050051, 0x00000006, 0x00000578, 0x00000E52, 0x00000001,
    0x00050051, 0x00000006, 0x0000057A, 0x00000E58, 0x00000000, 0x00050051,
    0x00000006, 0x0000057C, 0x00000E58, 0x00000001, 0x00070050, 0x0000000D,
    0x0000057D, 0x00000576, 0x00000578, 0x0000057A, 0x0000057C, 0x000500AA,
    0x0000003A, 0x00000BA0, 0x000006FF, 0x00000101, 0x000300F7, 0x00000BA5,
    0x00000000, 0x000400FA, 0x00000BA0, 0x00000BA1, 0x00000BA5, 0x000200F8,
    0x00000BA1, 0x00050051, 0x00000006, 0x00000BA3, 0x00000E39, 0x00000000,
    0x000500AB, 0x0000003A, 0x00000BA4, 0x00000BA3, 0x00000101, 0x000200F9,
    0x00000BA5, 0x000200F8, 0x00000BA5, 0x000700F5, 0x0000003A, 0x00000BA6,
    0x00000BA0, 0x00000B54, 0x00000BA4, 0x00000BA1, 0x000300F7, 0x00000BC4,
    0x00000002, 0x000400FA, 0x00000BA6, 0x00000BA7, 0x00000BC4, 0x000200F8,
    0x00000BA7, 0x00050051, 0x00000006, 0x00000BA9, 0x00000E39, 0x00000000,
    0x000500AE, 0x0000003A, 0x00000BAA, 0x00000BA9, 0x000000AA, 0x000300F7,
    0x00000BBD, 0x00000000, 0x000400FA, 0x00000BAA, 0x00000BAB, 0x00000BBD,
    0x000200F8, 0x00000BAB, 0x000500AE, 0x0000003A, 0x00000BAE, 0x00000BA9,
    0x000000C0, 0x000300F7, 0x00000BB6, 0x00000000, 0x000400FA, 0x00000BAE,
    0x00000BAF, 0x00000BB6, 0x000200F8, 0x00000BAF, 0x00060052, 0x0000000D,
    0x00000E28, 0x0000057A, 0x0000057D, 0x00000000, 0x00060052, 0x0000000D,
    0x00000E2A, 0x0000057C, 0x00000E28, 0x00000001, 0x000200F9, 0x00000BB6,
    0x000200F8, 0x00000BB6, 0x000700F5, 0x0000000D, 0x00000E5A, 0x0000057D,
    0x00000BAB, 0x00000E2A, 0x00000BAF, 0x00050051, 0x00000006, 0x00000BBA,
    0x00000E5A, 0x00000000, 0x00060052, 0x0000000D, 0x00000E2C, 0x00000BBA,
    0x00000574, 0x00000002, 0x00050051, 0x00000006, 0x00000BBC, 0x00000E5A,
    0x00000001, 0x00060052, 0x0000000D, 0x00000E2E, 0x00000BBC, 0x00000E2C,
    0x00000003, 0x000200F9, 0x00000BBD, 0x000200F8, 0x00000BBD, 0x000700F5,
    0x0000000D, 0x00000E60, 0x0000057D, 0x00000BA7, 0x00000E5A, 0x00000BB6,
    0x000700F5, 0x0000000D, 0x00000E5D, 0x00000574, 0x00000BA7, 0x00000E2E,
    0x00000BB6, 0x00050051, 0x00000006, 0x00000BC1, 0x00000E5D, 0x00000002,
    0x00060052, 0x0000000D, 0x00000E30, 0x00000BC1, 0x00000E5D, 0x00000000,
    0x00050051, 0x00000006, 0x00000BC3, 0x00000E5D, 0x00000003, 0x00060052,
    0x0000000D, 0x00000E32, 0x00000BC3, 0x00000E30, 0x00000001, 0x000200F9,
    0x00000BC4, 0x000200F8, 0x00000BC4, 0x000700F5, 0x0000000D, 0x00000E5F,
    0x0000057D, 0x00000BA5, 0x00000E60, 0x00000BBD, 0x000700F5, 0x0000000D,
    0x00000E5E, 0x00000574, 0x00000BA5, 0x00000E32, 0x00000BBD, 0x000300F7,
    0x00000BE5, 0x00000002, 0x000400FA, 0x0000060D, 0x00000BC8, 0x00000BE5,
    0x000200F8, 0x00000BC8, 0x000500AA, 0x0000003A, 0x00000BCB, 0x000005C1,
    0x00000163, 0x000400A8, 0x0000003A, 0x00000BCC, 0x00000BCB, 0x000300F7,
    0x00000BD1, 0x00000000, 0x000400FA, 0x00000BCC, 0x00000BCD, 0x00000BD1,
    0x000200F8, 0x00000BCD, 0x000500AA, 0x0000003A, 0x00000BD0, 0x000005C1,
    0x000001D8, 0x000200F9, 0x00000BD1, 0x000200F8, 0x00000BD1, 0x000700F5,
    0x0000003A, 0x00000BD2, 0x00000BCB, 0x00000BC8, 0x00000BD0, 0x00000BCD,
    0x000300F7, 0x00000BE4, 0x00000002, 0x000400FA, 0x00000BD2, 0x00000BD3,
    0x00000BE4, 0x000200F8, 0x00000BD3, 0x000500C7, 0x0000000D, 0x00000BD6,
    0x00000E5E, 0x00000E80, 0x0009004F, 0x0000000D, 0x00000BD8, 0x00000E5E,
    0x00000E5E, 0x00000001, 0x00000000, 0x00000003, 0x00000002, 0x000500C7,
    0x0000000D, 0x00000BDA, 0x00000BD8, 0x00000E81, 0x000500C5, 0x0000000D,
    0x00000BDB, 0x00000BD6, 0x00000BDA, 0x000500C7, 0x0000000D, 0x00000BDE,
    0x00000E5F, 0x00000E80, 0x0009004F, 0x0000000D, 0x00000BE0, 0x00000E5F,
    0x00000E5F, 0x00000001, 0x00000000, 0x00000003, 0x00000002, 0x000500C7,
    0x0000000D, 0x00000BE2, 0x00000BE0, 0x00000E81, 0x000500C5, 0x0000000D,
    0x00000BE3, 0x00000BDE, 0x00000BE2, 0x000200F9, 0x00000BE4, 0x000200F8,
    0x00000BE4, 0x000700F5, 0x0000000D, 0x00000E66, 0x00000E5F, 0x00000BD1,
    0x00000BE3, 0x00000BD3, 0x000700F5, 0x0000000D, 0x00000E64, 0x00000E5E,
    0x00000BD1, 0x00000BDB, 0x00000BD3, 0x000200F9, 0x00000BE5, 0x000200F8,
    0x00000BE5, 0x000700F5, 0x0000000D, 0x00000E65, 0x00000E5F, 0x00000BC4,
    0x00000E66, 0x00000BE4, 0x000700F5, 0x0000000D, 0x00000E63, 0x00000E5E,
    0x00000BC4, 0x00000E64, 0x00000BE4, 0x00050080, 0x00000008, 0x00000BED,
    0x00000E3B, 0x00000620, 0x000500C2, 0x00000008, 0x00000C20, 0x00000BED,
    0x00000E82, 0x00050086, 0x00000008, 0x00000C22, 0x00000C20, 0x000005CB,
    0x00050084, 0x00000008, 0x00000C25, 0x000005CB, 0x00000C22, 0x00050082,
    0x00000008, 0x00000C26, 0x00000C20, 0x00000C25, 0x000500C4, 0x00000008,
    0x00000C29, 0x00000C22, 0x00000E82, 0x00050051, 0x00000006, 0x00000C2C,
    0x00000C26, 0x00000000, 0x00050084, 0x00000006, 0x00000C2E, 0x00000C2C,
    0x0000078C, 0x00050051, 0x00000006, 0x00000C30, 0x00000C26, 0x00000001,
    0x00050080, 0x00000006, 0x00000C31, 0x00000C2E, 0x00000C30, 0x000500C4,
    0x00000008, 0x00000C37, 0x00000E7C, 0x00000E82, 0x00050082, 0x00000008,
    0x00000C39, 0x00000C37, 0x00000E7C, 0x000500C7, 0x00000008, 0x00000C3A,
    0x00000BED, 0x00000C39, 0x000500C4, 0x00000006, 0x00000C40, 0x00000C31,
    0x00000E83, 0x00050051, 0x00000006, 0x00000C42, 0x00000C3A, 0x00000001,
    0x000500C4, 0x00000006, 0x00000C44, 0x00000C42, 0x00000163, 0x000500C5,
    0x00000006, 0x00000C45, 0x00000C40, 0x00000C44, 0x00050051, 0x00000006,
    0x00000C47, 0x00000C3A, 0x00000000, 0x000500C4, 0x00000006, 0x00000C48,
    0x00000C47, 0x000000C0, 0x000500C5, 0x00000006, 0x00000C49, 0x00000C45,
    0x00000C48, 0x000300F7, 0x00000C06, 0x00000002, 0x000400FA, 0x000005F8,
    0x00000BF5, 0x00000C00, 0x000200F8, 0x00000C00, 0x0004007C, 0x00000020,
    0x00000C02, 0x00000C29, 0x00050051, 0x00000018, 0x00000CAD, 0x00000C02,
    0x00000001, 0x000500C3, 0x00000018, 0x00000CAE, 0x00000CAD, 0x000000EC,
    0x0004007C, 0x00000018, 0x00000CAF, 0x00000610, 0x00050084, 0x00000018,
    0x00000CB0, 0x00000CAE, 0x00000CAF, 0x00050051, 0x00000018, 0x00000CB1,
    0x00000C02, 0x00000000, 0x000500C3, 0x00000018, 0x00000CB2, 0x00000CB1,
    0x000000EC, 0x00050080, 0x00000018, 0x00000CB3, 0x00000CB0, 0x00000CB2,
    0x000500C4, 0x00000018, 0x00000CB4, 0x00000CB3, 0x000000E0, 0x000500C3,
    0x00000018, 0x00000CB6, 0x00000CAD, 0x000000EA, 0x000500C7, 0x00000018,
    0x00000CB7, 0x00000CB6, 0x000000F0, 0x000500C4, 0x00000018, 0x00000CB8,
    0x00000CB7, 0x0000010A, 0x000500C7, 0x00000018, 0x00000CBA, 0x00000CB1,
    0x000000F0, 0x000500C5, 0x00000018, 0x00000CBB, 0x00000CB8, 0x00000CBA,
    0x000500C5, 0x00000018, 0x00000CBE, 0x00000CB4, 0x00000CBB, 0x000500C4,
    0x00000018, 0x00000CBF, 0x00000CBE, 0x000000C0, 0x000500C3, 0x00000018,
    0x00000CC1, 0x00000CAD, 0x000000DE, 0x000500C7, 0x00000018, 0x00000CC2,
    0x00000CC1, 0x000000EA, 0x000500C3, 0x00000018, 0x00000CC4, 0x00000CB1,
    0x0000010A, 0x000500C7, 0x00000018, 0x00000CC5, 0x00000CC4, 0x0000010A,
    0x000500C3, 0x00000018, 0x00000CC7, 0x00000CAD, 0x0000010A, 0x000500C7,
    0x00000018, 0x00000CC8, 0x00000CC7, 0x000000EA, 0x000500C4, 0x00000018,
    0x00000CC9, 0x00000CC8, 0x000000EA, 0x000500C6, 0x00000018, 0x00000CCA,
    0x00000CC5, 0x00000CC9, 0x000500C7, 0x00000018, 0x00000CCF, 0x00000CAD,
    0x000000EA, 0x000500C4, 0x00000018, 0x00000CD3, 0x00000CCF, 0x000000DE,
    0x000500C4, 0x00000018, 0x00000CD4, 0x00000CCA, 0x000000E0, 0x000500C5,
    0x00000018, 0x00000CD5, 0x00000CD3, 0x00000CD4, 0x000500C4, 0x00000018,
    0x00000CD6, 0x00000CC2, 0x000000E3, 0x000500C5, 0x00000018, 0x00000CD7,
    0x00000CD5, 0x00000CD6, 0x000500C7, 0x00000018, 0x00000CD8, 0x00000CBF,
    0x000000E6, 0x000500C5, 0x00000018, 0x00000CD9, 0x00000CD7, 0x00000CD8,
    0x000500C3, 0x00000018, 0x00000CDA, 0x00000CBF, 0x000000DE, 0x000500C7,
    0x00000018, 0x00000CDB, 0x00000CDA, 0x000000EA, 0x000500C4, 0x00000018,
    0x00000CDC, 0x00000CDB, 0x000000EC, 0x000500C5, 0x00000018, 0x00000CDD,
    0x00000CD9, 0x00000CDC, 0x000500C3, 0x00000018, 0x00000CDE, 0x00000CBF,
    0x000000EC, 0x000500C7, 0x00000018, 0x00000CDF, 0x00000CDE, 0x000000F0,
    0x000500C4, 0x00000018, 0x00000CE0, 0x00000CDF, 0x000000F2, 0x000500C5,
    0x00000018, 0x00000CE1, 0x00000CDD, 0x00000CE0, 0x000500C3, 0x00000018,
    0x00000CE2, 0x00000CBF, 0x000000F2, 0x000500C4, 0x00000018, 0x00000CE3,
    0x00000CE2, 0x000000F6, 0x000500C5, 0x00000018, 0x00000CE4, 0x00000CE1,
    0x00000CE3, 0x0004007C, 0x00000006, 0x00000C05, 0x00000CE4, 0x000200F9,
    0x00000C06, 0x000200F8, 0x00000BF5, 0x00050051, 0x00000006, 0x00000BF8,
    0x00000C29, 0x00000000, 0x00050051, 0x00000006, 0x00000BF9, 0x00000C29,
    0x00000001, 0x00060050, 0x00000263, 0x00000BFA, 0x00000BF8, 0x00000BF9,
    0x000005FC, 0x0004007C, 0x00000027, 0x00000BFB, 0x00000BFA, 0x00050051,
    0x00000018, 0x00000C64, 0x00000BFB, 0x00000002, 0x000500C3, 0x00000018,
    0x00000C65, 0x00000C64, 0x0000012B, 0x0004007C, 0x00000018, 0x00000C66,
    0x00000615, 0x00050084, 0x00000018, 0x00000C67, 0x00000C65, 0x00000C66,
    0x00050051, 0x00000018, 0x00000C68, 0x00000BFB, 0x00000001, 0x000500C3,
    0x00000018, 0x00000C69, 0x00000C68, 0x000000DE, 0x00050080, 0x00000018,
    0x00000C6A, 0x00000C67, 0x00000C69, 0x0004007C, 0x00000018, 0x00000C6B,
    0x00000610, 0x00050084, 0x00000018, 0x00000C6C, 0x00000C6A, 0x00000C6B,
    0x00050051, 0x00000018, 0x00000C6D, 0x00000BFB, 0x00000000, 0x000500C3,
    0x00000018, 0x00000C6E, 0x00000C6D, 0x000000EC, 0x00050080, 0x00000018,
    0x00000C6F, 0x00000C6C, 0x00000C6E, 0x000500C4, 0x00000018, 0x00000C70,
    0x00000C6F, 0x000000F0, 0x000500C7, 0x00000018, 0x00000C72, 0x00000C64,
    0x0000010A, 0x000500C4, 0x00000018, 0x00000C73, 0x00000C72, 0x000000EC,
    0x000500C3, 0x00000018, 0x00000C75, 0x00000C68, 0x000000EA, 0x000500C7,
    0x00000018, 0x00000C76, 0x00000C75, 0x0000010A, 0x000500C4, 0x00000018,
    0x00000C77, 0x00000C76, 0x0000010A, 0x000500C5, 0x00000018, 0x00000C78,
    0x00000C73, 0x00000C77, 0x000500C7, 0x00000018, 0x00000C7A, 0x00000C6D,
    0x000000F0, 0x000500C5, 0x00000018, 0x00000C7B, 0x00000C78, 0x00000C7A,
    0x000500C5, 0x00000018, 0x00000C7E, 0x00000C70, 0x00000C7B, 0x000500C4,
    0x00000018, 0x00000C7F, 0x00000C7E, 0x000000C0, 0x000500C3, 0x00000018,
    0x00000C81, 0x00000C68, 0x0000010A, 0x000500C6, 0x00000018, 0x00000C84,
    0x00000C81, 0x00000C65, 0x000500C7, 0x00000018, 0x00000C85, 0x00000C84,
    0x000000EA, 0x000500C3, 0x00000018, 0x00000C87, 0x00000C6D, 0x0000010A,
    0x000500C7, 0x00000018, 0x00000C88, 0x00000C87, 0x0000010A, 0x000500C4,
    0x00000018, 0x00000C8A, 0x00000C85, 0x000000EA, 0x000500C6, 0x00000018,
    0x00000C8B, 0x00000C88, 0x00000C8A, 0x000500C7, 0x00000018, 0x00000C90,
    0x00000C68, 0x000000EA, 0x000500C4, 0x00000018, 0x00000C94, 0x00000C90,
    0x000000DE, 0x000500C4, 0x00000018, 0x00000C95, 0x00000C8B, 0x000000E0,
    0x000500C5, 0x00000018, 0x00000C96, 0x00000C94, 0x00000C95, 0x000500C4,
    0x00000018, 0x00000C97, 0x00000C85, 0x000000E3, 0x000500C5, 0x00000018,
    0x00000C98, 0x00000C96, 0x00000C97, 0x000500C7, 0x00000018, 0x00000C99,
    0x00000C7F, 0x000000E6, 0x000500C5, 0x00000018, 0x00000C9A, 0x00000C98,
    0x00000C99, 0x000500C3, 0x00000018, 0x00000C9B, 0x00000C7F, 0x000000DE,
    0x000500C7, 0x00000018, 0x00000C9C, 0x00000C9B, 0x000000EA, 0x000500C4,
    0x00000018, 0x00000C9D, 0x00000C9C, 0x000000EC, 0x000500C5, 0x00000018,
    0x00000C9E, 0x00000C9A, 0x00000C9D, 0x000500C3, 0x00000018, 0x00000C9F,
    0x00000C7F, 0x000000EC, 0x000500C7, 0x00000018, 0x00000CA0, 0x00000C9F,
    0x000000F0, 0x000500C4, 0x00000018, 0x00000CA1, 0x00000CA0, 0x000000F2,
    0x000500C5, 0x00000018, 0x00000CA2, 0x00000C9E, 0x00000CA1, 0x000500C3,
    0x00000018, 0x00000CA3, 0x00000C7F, 0x000000F2, 0x000500C4, 0x00000018,
    0x00000CA4, 0x00000CA3, 0x000000F6, 0x000500C5, 0x00000018, 0x00000CA5,
    0x00000CA2, 0x00000CA4, 0x0004007C, 0x00000006, 0x00000BFF, 0x00000CA5,
    0x000200F9, 0x00000C06, 0x000200F8, 0x00000C06, 0x000700F5, 0x00000006,
    0x00000E68, 0x00000BFF, 0x00000BF5, 0x00000C05, 0x00000C00, 0x00050084,
    0x00000006, 0x00000C0A, 0x000005EC, 0x0000078C, 0x00050084, 0x00000006,
    0x00000C0B, 0x00000E68, 0x00000C0A, 0x00050080, 0x00000006, 0x00000C0E,
    0x00000C0B, 0x00000C49, 0x000500C2, 0x00000006, 0x0000058F, 0x00000C0E,
    0x000000DE, 0x000500AA, 0x0000003A, 0x00000CEA, 0x000005F4, 0x000000D1,
    0x000300F7, 0x00000CEE, 0x00000000, 0x000400FA, 0x00000CEA, 0x00000CEB,
    0x00000CEE, 0x000200F8, 0x00000CEB, 0x0009004F, 0x0000000D, 0x00000CED,
    0x00000E63, 0x00000E63, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
    0x000200F9, 0x00000CEE, 0x000200F8, 0x00000CEE, 0x000700F5, 0x0000000D,
    0x00000E6B, 0x00000E63, 0x00000C06, 0x00000CED, 0x00000CEB, 0x000600A9,
    0x00000006, 0x00000E8D, 0x00000CEA, 0x000000AA, 0x000005F4, 0x000500AA,
    0x0000003A, 0x00000CF5, 0x00000E8D, 0x000000A7, 0x000500AA, 0x0000003A,
    0x00000CF7, 0x00000E8D, 0x000000AA, 0x000500A6, 0x0000003A, 0x00000CF8,
    0x00000CF5, 0x00000CF7, 0x000300F7, 0x00000D05, 0x00000000, 0x000400FA,
    0x00000CF8, 0x00000CF9, 0x00000D05, 0x000200F8, 0x00000CF9, 0x000500C7,
    0x0000000D, 0x00000CFC, 0x00000E6B, 0x00000E84, 0x000500C4, 0x0000000D,
    0x00000CFE, 0x00000CFC, 0x00000E85, 0x000500C7, 0x0000000D, 0x00000D01,
    0x00000E6B, 0x00000E86, 0x000500C2, 0x0000000D, 0x00000D03, 0x00000D01,
    0x00000E85, 0x000500C5, 0x0000000D, 0x00000D04, 0x00000CFE, 0x00000D03,
    0x000200F9, 0x00000D05, 0x000200F8, 0x00000D05, 0x000700F5, 0x0000000D,
    0x00000E6D, 0x00000E6B, 0x00000CEE, 0x00000D04, 0x00000CF9, 0x000500AA,
    0x0000003A, 0x00000D09, 0x00000E8D, 0x000000C0, 0x000500A6, 0x0000003A,
    0x00000D0A, 0x00000CF7, 0x00000D09, 0x000300F7, 0x00000D13, 0x00000000,
    0x000400FA, 0x00000D0A, 0x00000D0B, 0x00000D13, 0x000200F8, 0x00000D0B,
    0x000500C4, 0x0000000D, 0x00000D0E, 0x00000E6D, 0x00000E87, 0x000500C2,
    0x0000000D, 0x00000D11, 0x00000E6D, 0x00000E87, 0x000500C5, 0x0000000D,
    0x00000D12, 0x00000D0E, 0x00000D11, 0x000200F9, 0x00000D13, 0x000200F8,
    0x00000D13, 0x000700F5, 0x0000000D, 0x00000E6E, 0x00000E6D, 0x00000D05,
    0x00000D12, 0x00000D0B, 0x00060041, 0x000004E9, 0x00000594, 0x000004E0,
    0x0000017B, 0x0000058F, 0x0003003E, 0x00000594, 0x00000E6E, 0x00050080,
    0x00000006, 0x00000597, 0x00000C0E, 0x000000C6, 0x000500C2, 0x00000006,
    0x00000599, 0x00000597, 0x000000DE, 0x000300F7, 0x00000D21, 0x00000000,
    0x000400FA, 0x00000CEA, 0x00000D1E, 0x00000D21, 0x000200F8, 0x00000D1E,
    0x0009004F, 0x0000000D, 0x00000D20, 0x00000E65, 0x00000E65, 0x00000001,
    0x00000000, 0x00000003, 0x00000002, 0x000200F9, 0x00000D21, 0x000200F8,
    0x00000D21, 0x000700F5, 0x0000000D, 0x00000E77, 0x00000E65, 0x00000D13,
    0x00000D20, 0x00000D1E, 0x000300F7, 0x00000D38, 0x00000000, 0x000400FA,
    0x00000CF8, 0x00000D2C, 0x00000D38, 0x000200F8, 0x00000D2C, 0x000500C7,
    0x0000000D, 0x00000D2F, 0x00000E77, 0x00000E84, 0x000500C4, 0x0000000D,
    0x00000D31, 0x00000D2F, 0x00000E85, 0x000500C7, 0x0000000D, 0x00000D34,
    0x00000E77, 0x00000E86, 0x000500C2, 0x0000000D, 0x00000D36, 0x00000D34,
    0x00000E85, 0x000500C5, 0x0000000D, 0x00000D37, 0x00000D31, 0x00000D36,
    0x000200F9, 0x00000D38, 0x000200F8, 0x00000D38, 0x000700F5, 0x0000000D,
    0x00000E79, 0x00000E77, 0x00000D21, 0x00000D37, 0x00000D2C, 0x000300F7,
    0x00000D46, 0x00000000, 0x000400FA, 0x00000D0A, 0x00000D3E, 0x00000D46,
    0x000200F8, 0x00000D3E, 0x000500C4, 0x0000000D, 0x00000D41, 0x00000E79,
    0x00000E87, 0x000500C2, 0x0000000D, 0x00000D44, 0x00000E79, 0x00000E87,
    0x000500C5, 0x0000000D, 0x00000D45, 0x00000D41, 0x00000D44, 0x000200F9,
    0x00000D46, 0x000200F8, 0x00000D46, 0x000700F5, 0x0000000D, 0x00000E7A,
    0x00000E79, 0x00000D38, 0x00000D45, 0x00000D3E, 0x00060041, 0x000004E9,
    0x0000059E, 0x000004E0, 0x0000017B, 0x00000599, 0x0003003E, 0x0000059E,
    0x00000E7A, 0x000200F9, 0x0000059F, 0x000200F8, 0x0000059F, 0x000100FD,
    0x00010038,
};
