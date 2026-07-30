// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.3
; Generator: Google Shaderc over Glslang; 10
; Bound: 4379
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
               OpMemberName %push_const_block_xe 4 "xe_resolve_dest_base"
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
               OpMemberDecorate %push_const_block_xe 4 Offset 16
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
     %uint_1 = OpConstant %uint 1
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
     %uint_8 = OpConstant %uint 8
%uint_4278255360 = OpConstant %uint 4278255360
     %uint_3 = OpConstant %uint 3
    %uint_16 = OpConstant %uint 16
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
      %int_0 = OpConstant %int 0
%push_const_block_xe = OpTypeStruct %uint %uint %uint %uint %uint
%_ptr_PushConstant_push_const_block_xe = OpTypePointer PushConstant %push_const_block_xe
%push_consts_xe = OpVariable %_ptr_PushConstant_push_const_block_xe PushConstant
%_ptr_PushConstant_uint = OpTypePointer PushConstant %uint
  %uint_1023 = OpConstant %uint 1023
    %uint_10 = OpConstant %uint 10
  %uint_2047 = OpConstant %uint 2047
    %uint_24 = OpConstant %uint 24
    %uint_15 = OpConstant %uint 15
    %uint_28 = OpConstant %uint 28
     %uint_4 = OpConstant %uint 4
        %387 = OpConstantComposite %v2uint %uint_0 %uint_4
        %391 = OpConstantComposite %v2uint %uint_4 %uint_1
      %int_9 = OpConstant %int 9
     %uint_5 = OpConstant %uint 5
     %uint_7 = OpConstant %uint 7
%uint_16777216 = OpConstant %uint 16777216
    %uint_20 = OpConstant %uint 20
        %470 = OpConstantComposite %v2uint %uint_20 %uint_24
     %v3uint = OpTypeVector %uint 3
   %uint_255 = OpConstant %uint 255
%uint_3222273024 = OpConstant %uint 3222273024
    %uint_80 = OpConstant %uint 80
 %uint_65535 = OpConstant %uint 65535
%xe_resolve_host_color_xe_block = OpTypeStruct %uint %uint %uint %uint %uint %uint %uint %uint %uint %uint %uint
%_ptr_Uniform_xe_resolve_host_color_xe_block = OpTypePointer Uniform %xe_resolve_host_color_xe_block
%xe_resolve_host_color = OpVariable %_ptr_Uniform_xe_resolve_host_color_xe_block Uniform
%_ptr_Uniform_uint = OpTypePointer Uniform %uint
        %733 = OpConstantComposite %v2uint %uint_1 %uint_0
        %764 = OpTypeImage %uint 2D 0 0 1 1 Unknown
%_ptr_UniformConstant_764 = OpTypePointer UniformConstant %764
%xe_resolve_host_color_source = OpVariable %_ptr_UniformConstant_764 UniformConstant
      %false = OpConstantFalse %bool
       %true = OpConstantTrue %bool
        %932 = OpConstantComposite %v2uint %uint_0 %uint_1
     %uint_6 = OpConstant %uint 6
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%xe_resolve_dest_xe_block = OpTypeStruct %_runtimearr_v4uint
%_ptr_StorageBuffer_xe_resolve_dest_xe_block = OpTypePointer StorageBuffer %xe_resolve_dest_xe_block
%xe_resolve_dest = OpVariable %_ptr_StorageBuffer_xe_resolve_dest_xe_block StorageBuffer
%_ptr_StorageBuffer_v4uint = OpTypePointer StorageBuffer %v4uint
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %4169 = OpUndef %v2uint
       %4352 = OpConstantComposite %v2uint %uint_1 %uint_1
       %4354 = OpConstantComposite %v2uint %uint_3 %uint_3
       %4355 = OpConstantComposite %v2uint %uint_15 %uint_15
       %4356 = OpConstantComposite %v4uint %uint_4278255360 %uint_4278255360 %uint_4278255360 %uint_4278255360
       %4357 = OpConstantComposite %v4uint %uint_255 %uint_255 %uint_255 %uint_255
       %4358 = OpConstantComposite %v4uint %uint_16 %uint_16 %uint_16 %uint_16
       %4359 = OpConstantComposite %v4uint %uint_3222273024 %uint_3222273024 %uint_3222273024 %uint_3222273024
       %4360 = OpConstantComposite %v4uint %uint_1023 %uint_1023 %uint_1023 %uint_1023
       %4361 = OpConstantComposite %v4uint %uint_20 %uint_20 %uint_20 %uint_20
       %4362 = OpConstantComposite %v4uint %uint_16711935 %uint_16711935 %uint_16711935 %uint_16711935
       %4363 = OpConstantComposite %v4uint %uint_8 %uint_8 %uint_8 %uint_8
    %uint_32 = OpConstant %uint 32
       %main = OpFunction %void None %3
          %5 = OpLabel
       %1180 = OpLoad %v3uint %gl_GlobalInvocationID
               OpSelectionMerge %1388 None
               OpSwitch %uint_0 %1276
       %1276 = OpLabel
       %1401 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_0
       %1402 = OpLoad %uint %1401
       %1403 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_1
       %1404 = OpLoad %uint %1403
       %1421 = OpShiftRightLogical %uint %1402 %uint_24
       %1422 = OpBitwiseAnd %uint %1421 %uint_15
       %1526 = OpCompositeConstruct %v2uint %1404 %1404
       %1434 = OpShiftRightLogical %v2uint %1526 %387
       %1436 = OpShiftLeftLogical %v2uint %4352 %391
       %1438 = OpISub %v2uint %1436 %4352
       %1439 = OpBitwiseAnd %v2uint %1434 %1438
       %1441 = OpShiftLeftLogical %v2uint %1439 %4354
       %1444 = OpIMul %v2uint %1441 %4352
       %1447 = OpShiftRightLogical %uint %1404 %uint_5
       %1448 = OpBitwiseAnd %uint %1447 %uint_2047
       %1453 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_2
       %1454 = OpLoad %uint %1453
       %1455 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_3
       %1456 = OpLoad %uint %1455
       %1458 = OpBitwiseAnd %uint %1454 %uint_7
       %1461 = OpBitwiseAnd %uint %1454 %uint_8
       %1462 = OpINotEqual %bool %1461 %uint_0
       %1465 = OpShiftRightLogical %uint %1454 %uint_4
       %1466 = OpBitwiseAnd %uint %1465 %uint_7
       %1482 = OpBitwiseAnd %uint %1454 %uint_16777216
       %1483 = OpINotEqual %bool %1482 %uint_0
       %1486 = OpBitwiseAnd %uint %1456 %uint_1023
       %1489 = OpShiftRightLogical %uint %1456 %uint_10
       %1490 = OpBitwiseAnd %uint %1489 %uint_1023
       %1491 = OpShiftLeftLogical %uint %1490 %int_1
       %1536 = OpCompositeConstruct %v2uint %1456 %1456
       %1495 = OpShiftRightLogical %v2uint %1536 %470
       %1497 = OpBitwiseAnd %v2uint %1495 %4355
       %1499 = OpShiftLeftLogical %v2uint %1497 %4354
       %1502 = OpIMul %v2uint %1499 %4352
       %1505 = OpShiftRightLogical %uint %1456 %uint_28
       %1506 = OpBitwiseAnd %uint %1505 %uint_7
       %1508 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_4
       %1509 = OpLoad %uint %1508
               OpSelectionMerge %1668 None
               OpSwitch %uint_0 %1557
       %1557 = OpLabel
       %1559 = OpCompositeExtract %uint %1180 0
       %1560 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_5
       %1561 = OpLoad %uint %1560
       %1562 = OpUGreaterThanEqual %bool %1559 %1561
       %1563 = OpLogicalNot %bool %1562
               OpSelectionMerge %1570 None
               OpBranchConditional %1563 %1564 %1570
       %1564 = OpLabel
       %1566 = OpCompositeExtract %uint %1180 1
       %1567 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_6
       %1568 = OpLoad %uint %1567
       %1569 = OpUGreaterThanEqual %bool %1566 %1568
               OpBranch %1570
       %1570 = OpLabel
       %1571 = OpPhi %bool %1562 %1557 %1569 %1564
               OpSelectionMerge %1573 None
               OpBranchConditional %1571 %1572 %1573
       %1572 = OpLabel
               OpBranch %1668
       %1573 = OpLabel
       %1582 = OpIMul %uint %1559 %uint_8
       %1584 = OpCompositeExtract %uint %1180 1
       %1587 = OpUDiv %uint %1582 %uint_80
       %1590 = OpUDiv %uint %1584 %uint_8
       %1594 = OpIMul %uint %1587 %uint_80
       %1595 = OpISub %uint %1582 %1594
       %1599 = OpIMul %uint %1590 %uint_8
       %1600 = OpISub %uint %1584 %1599
       %1601 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_0
       %1602 = OpLoad %uint %1601
       %1604 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_2
       %1605 = OpLoad %uint %1604
       %1606 = OpIMul %uint %1590 %1605
       %1607 = OpIAdd %uint %1602 %1606
       %1609 = OpIAdd %uint %1607 %1587
       %1614 = OpUDiv %uint %1609 %1605
       %1618 = OpIMul %uint %1614 %1605
       %1619 = OpISub %uint %1609 %1618
       %1622 = OpIMul %uint %1619 %uint_80
       %1624 = OpIAdd %uint %1622 %1595
       %1627 = OpIMul %uint %1614 %uint_8
       %1629 = OpIAdd %uint %1627 %1600
       %1630 = OpCompositeConstruct %v2uint %1624 %1629
       %1634 = OpCompositeExtract %uint %1444 0
       %1635 = OpULessThan %bool %1624 %1634
       %1636 = OpLogicalNot %bool %1635
               OpSelectionMerge %1643 None
               OpBranchConditional %1636 %1637 %1643
       %1637 = OpLabel
       %1641 = OpCompositeExtract %uint %1444 1
       %1642 = OpULessThan %bool %1629 %1641
               OpBranch %1643
       %1643 = OpLabel
       %1644 = OpPhi %bool %1635 %1573 %1642 %1637
               OpSelectionMerge %1646 None
               OpBranchConditional %1644 %1645 %1646
       %1645 = OpLabel
               OpBranch %1668
       %1646 = OpLabel
       %1650 = OpISub %v2uint %1630 %1444
       %1652 = OpCompositeExtract %uint %1650 0
       %1655 = OpShiftLeftLogical %uint %1448 %uint_3
       %1656 = OpUGreaterThanEqual %bool %1652 %1655
       %1657 = OpLogicalNot %bool %1656
               OpSelectionMerge %1664 None
               OpBranchConditional %1657 %1658 %1664
       %1658 = OpLabel
       %1660 = OpCompositeExtract %uint %1650 1
       %1661 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_7
       %1662 = OpLoad %uint %1661
       %1663 = OpUGreaterThanEqual %bool %1660 %1662
               OpBranch %1664
       %1664 = OpLabel
       %1665 = OpPhi %bool %1656 %1646 %1663 %1658
               OpSelectionMerge %1667 None
               OpBranchConditional %1665 %1666 %1667
       %1666 = OpLabel
               OpBranch %1668
       %1667 = OpLabel
               OpBranch %1668
       %1668 = OpLabel
       %4167 = OpPhi %v2uint %4169 %1572 %4169 %1645 %1650 %1666 %1650 %1667
       %4166 = OpPhi %bool %false %1572 %false %1645 %false %1666 %true %1667
       %1282 = OpLogicalNot %bool %4166
               OpSelectionMerge %1284 None
               OpBranchConditional %1282 %1283 %1284
       %1283 = OpLabel
               OpBranch %1388
       %1284 = OpLabel
       %1711 = OpULessThanEqual %bool %1506 %uint_3
               OpSelectionMerge %1720 None
               OpBranchConditional %1711 %1712 %1714
       %1714 = OpLabel
       %1716 = OpIEqual %bool %1506 %uint_5
       %4370 = OpSelect %uint %1716 %uint_2 %uint_0
               OpBranch %1720
       %1712 = OpLabel
               OpBranch %1720
       %1720 = OpLabel
       %4172 = OpPhi %uint %1506 %1712 %4370 %1714
       %1727 = OpCompositeExtract %uint %4167 0
       %1731 = OpCompositeExtract %uint %4167 1
       %1734 = OpExtInst %uint %1 UMax %1731 %uint_0
       %1735 = OpCompositeConstruct %v2uint %1727 %1734
       %1738 = OpIAdd %v2uint %1735 %1444
       %1740 = OpShiftLeftLogical %v2uint %1738 %932
       %1756 = OpCompositeConstruct %v2uint %4172 %4172
       %1749 = OpShiftRightLogical %v2uint %1756 %733
       %1751 = OpBitwiseAnd %v2uint %1749 %4352
       %1743 = OpIAdd %v2uint %1740 %1751
       %1806 = OpCompositeExtract %uint %1743 0
       %1808 = OpUDiv %uint %1806 %uint_80
       %1810 = OpCompositeExtract %uint %1743 1
       %1812 = OpUDiv %uint %1810 %uint_16
       %1817 = OpIMul %uint %1808 %uint_80
       %1818 = OpISub %uint %1806 %1817
       %1823 = OpIMul %uint %1812 %uint_16
       %1824 = OpISub %uint %1810 %1823
       %1826 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_2
       %1827 = OpLoad %uint %1826
       %1828 = OpIMul %uint %1812 %1827
       %1830 = OpIAdd %uint %1828 %1808
       %1831 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_1
       %1832 = OpLoad %uint %1831
       %1834 = OpIAdd %uint %1832 %1830
       %1836 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_3
       %1837 = OpLoad %uint %1836
       %1838 = OpISub %uint %1834 %1837
       %1839 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_4
       %1840 = OpLoad %uint %1839
       %1843 = OpUDiv %uint %1838 %1840
       %1847 = OpIMul %uint %1843 %1840
       %1848 = OpISub %uint %1838 %1847
       %1851 = OpIMul %uint %1848 %uint_80
       %1853 = OpIAdd %uint %1851 %1818
       %1856 = OpIMul %uint %1843 %uint_16
       %1858 = OpIAdd %uint %1856 %1824
       %1874 = OpBitwiseAnd %uint %1858 %uint_1
       %1875 = OpINotEqual %bool %1874 %uint_0
               OpSelectionMerge %1882 None
               OpBranchConditional %1875 %1876 %1879
       %1879 = OpLabel
       %1880 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %1881 = OpLoad %uint %1880
               OpBranch %1882
       %1876 = OpLabel
       %1877 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %1878 = OpLoad %uint %1877
               OpBranch %1882
       %1882 = OpLabel
       %4174 = OpPhi %uint %1878 %1876 %1881 %1879
       %1770 = OpLoad %764 %xe_resolve_host_color_source
       %1773 = OpBitcast %int %1853
       %1776 = OpShiftRightLogical %uint %1858 %uint_1
       %1777 = OpBitcast %int %1776
       %1781 = OpCompositeConstruct %v2int %1773 %1777
       %1783 = OpBitcast %int %4174
       %1784 = OpImageFetch %v4uint %1770 %1781 Sample %1783
               OpSelectionMerge %1905 None
               OpSwitch %1422 %1890 4 %1893 6 %1893 14 %1902
       %1902 = OpLabel
       %1904 = OpCompositeExtract %uint %1784 0
               OpBranch %1905
       %1893 = OpLabel
       %1895 = OpCompositeExtract %uint %1784 0
       %1896 = OpBitwiseAnd %uint %1895 %uint_65535
       %1898 = OpCompositeExtract %uint %1784 1
       %1899 = OpBitwiseAnd %uint %1898 %uint_65535
       %1900 = OpShiftLeftLogical %uint %1899 %uint_16
       %1901 = OpBitwiseOr %uint %1896 %1900
               OpBranch %1905
       %1890 = OpLabel
       %1892 = OpCompositeExtract %uint %1784 0
               OpBranch %1905
       %1905 = OpLabel
       %4177 = OpPhi %uint %1892 %1890 %1901 %1893 %1904 %1902
               OpSelectionMerge %1937 None
               OpBranchConditional %1711 %1929 %1931
       %1931 = OpLabel
       %1933 = OpIEqual %bool %1506 %uint_5
       %4371 = OpSelect %uint %1933 %uint_2 %uint_0
               OpBranch %1937
       %1929 = OpLabel
               OpBranch %1937
       %1937 = OpLabel
       %4189 = OpPhi %uint %1506 %1929 %4371 %1931
       %1946 = OpIAdd %uint %1727 %uint_1
       %1952 = OpCompositeConstruct %v2uint %1946 %1734
       %1955 = OpIAdd %v2uint %1952 %1444
       %1957 = OpShiftLeftLogical %v2uint %1955 %932
       %1973 = OpCompositeConstruct %v2uint %4189 %4189
       %1966 = OpShiftRightLogical %v2uint %1973 %733
       %1968 = OpBitwiseAnd %v2uint %1966 %4352
       %1960 = OpIAdd %v2uint %1957 %1968
       %2023 = OpCompositeExtract %uint %1960 0
       %2025 = OpUDiv %uint %2023 %uint_80
       %2027 = OpCompositeExtract %uint %1960 1
       %2029 = OpUDiv %uint %2027 %uint_16
       %2034 = OpIMul %uint %2025 %uint_80
       %2035 = OpISub %uint %2023 %2034
       %2040 = OpIMul %uint %2029 %uint_16
       %2041 = OpISub %uint %2027 %2040
       %2045 = OpIMul %uint %2029 %1827
       %2047 = OpIAdd %uint %2045 %2025
       %2051 = OpIAdd %uint %1832 %2047
       %2055 = OpISub %uint %2051 %1837
       %2060 = OpUDiv %uint %2055 %1840
       %2064 = OpIMul %uint %2060 %1840
       %2065 = OpISub %uint %2055 %2064
       %2068 = OpIMul %uint %2065 %uint_80
       %2070 = OpIAdd %uint %2068 %2035
       %2073 = OpIMul %uint %2060 %uint_16
       %2075 = OpIAdd %uint %2073 %2041
       %2091 = OpBitwiseAnd %uint %2075 %uint_1
       %2092 = OpINotEqual %bool %2091 %uint_0
               OpSelectionMerge %2099 None
               OpBranchConditional %2092 %2093 %2096
       %2096 = OpLabel
       %2097 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2098 = OpLoad %uint %2097
               OpBranch %2099
       %2093 = OpLabel
       %2094 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2095 = OpLoad %uint %2094
               OpBranch %2099
       %2099 = OpLabel
       %4191 = OpPhi %uint %2095 %2093 %2098 %2096
       %1990 = OpBitcast %int %2070
       %1993 = OpShiftRightLogical %uint %2075 %uint_1
       %1994 = OpBitcast %int %1993
       %1998 = OpCompositeConstruct %v2int %1990 %1994
       %2000 = OpBitcast %int %4191
       %2001 = OpImageFetch %v4uint %1770 %1998 Sample %2000
               OpSelectionMerge %2122 None
               OpSwitch %1422 %2107 4 %2110 6 %2110 14 %2119
       %2119 = OpLabel
       %2121 = OpCompositeExtract %uint %2001 0
               OpBranch %2122
       %2110 = OpLabel
       %2112 = OpCompositeExtract %uint %2001 0
       %2113 = OpBitwiseAnd %uint %2112 %uint_65535
       %2115 = OpCompositeExtract %uint %2001 1
       %2116 = OpBitwiseAnd %uint %2115 %uint_65535
       %2117 = OpShiftLeftLogical %uint %2116 %uint_16
       %2118 = OpBitwiseOr %uint %2113 %2117
               OpBranch %2122
       %2107 = OpLabel
       %2109 = OpCompositeExtract %uint %2001 0
               OpBranch %2122
       %2122 = OpLabel
       %4194 = OpPhi %uint %2109 %2107 %2118 %2110 %2121 %2119
               OpSelectionMerge %2154 None
               OpBranchConditional %1711 %2146 %2148
       %2148 = OpLabel
       %2150 = OpIEqual %bool %1506 %uint_5
       %4372 = OpSelect %uint %2150 %uint_2 %uint_0
               OpBranch %2154
       %2146 = OpLabel
               OpBranch %2154
       %2154 = OpLabel
       %4201 = OpPhi %uint %1506 %2146 %4372 %2148
       %2163 = OpIAdd %uint %1727 %uint_2
       %2169 = OpCompositeConstruct %v2uint %2163 %1734
       %2172 = OpIAdd %v2uint %2169 %1444
       %2174 = OpShiftLeftLogical %v2uint %2172 %932
       %2190 = OpCompositeConstruct %v2uint %4201 %4201
       %2183 = OpShiftRightLogical %v2uint %2190 %733
       %2185 = OpBitwiseAnd %v2uint %2183 %4352
       %2177 = OpIAdd %v2uint %2174 %2185
       %2240 = OpCompositeExtract %uint %2177 0
       %2242 = OpUDiv %uint %2240 %uint_80
       %2244 = OpCompositeExtract %uint %2177 1
       %2246 = OpUDiv %uint %2244 %uint_16
       %2251 = OpIMul %uint %2242 %uint_80
       %2252 = OpISub %uint %2240 %2251
       %2257 = OpIMul %uint %2246 %uint_16
       %2258 = OpISub %uint %2244 %2257
       %2262 = OpIMul %uint %2246 %1827
       %2264 = OpIAdd %uint %2262 %2242
       %2268 = OpIAdd %uint %1832 %2264
       %2272 = OpISub %uint %2268 %1837
       %2277 = OpUDiv %uint %2272 %1840
       %2281 = OpIMul %uint %2277 %1840
       %2282 = OpISub %uint %2272 %2281
       %2285 = OpIMul %uint %2282 %uint_80
       %2287 = OpIAdd %uint %2285 %2252
       %2290 = OpIMul %uint %2277 %uint_16
       %2292 = OpIAdd %uint %2290 %2258
       %2308 = OpBitwiseAnd %uint %2292 %uint_1
       %2309 = OpINotEqual %bool %2308 %uint_0
               OpSelectionMerge %2316 None
               OpBranchConditional %2309 %2310 %2313
       %2313 = OpLabel
       %2314 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2315 = OpLoad %uint %2314
               OpBranch %2316
       %2310 = OpLabel
       %2311 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2312 = OpLoad %uint %2311
               OpBranch %2316
       %2316 = OpLabel
       %4203 = OpPhi %uint %2312 %2310 %2315 %2313
       %2207 = OpBitcast %int %2287
       %2210 = OpShiftRightLogical %uint %2292 %uint_1
       %2211 = OpBitcast %int %2210
       %2215 = OpCompositeConstruct %v2int %2207 %2211
       %2217 = OpBitcast %int %4203
       %2218 = OpImageFetch %v4uint %1770 %2215 Sample %2217
               OpSelectionMerge %2339 None
               OpSwitch %1422 %2324 4 %2327 6 %2327 14 %2336
       %2336 = OpLabel
       %2338 = OpCompositeExtract %uint %2218 0
               OpBranch %2339
       %2327 = OpLabel
       %2329 = OpCompositeExtract %uint %2218 0
       %2330 = OpBitwiseAnd %uint %2329 %uint_65535
       %2332 = OpCompositeExtract %uint %2218 1
       %2333 = OpBitwiseAnd %uint %2332 %uint_65535
       %2334 = OpShiftLeftLogical %uint %2333 %uint_16
       %2335 = OpBitwiseOr %uint %2330 %2334
               OpBranch %2339
       %2324 = OpLabel
       %2326 = OpCompositeExtract %uint %2218 0
               OpBranch %2339
       %2339 = OpLabel
       %4206 = OpPhi %uint %2326 %2324 %2335 %2327 %2338 %2336
               OpSelectionMerge %2371 None
               OpBranchConditional %1711 %2363 %2365
       %2365 = OpLabel
       %2367 = OpIEqual %bool %1506 %uint_5
       %4373 = OpSelect %uint %2367 %uint_2 %uint_0
               OpBranch %2371
       %2363 = OpLabel
               OpBranch %2371
       %2371 = OpLabel
       %4213 = OpPhi %uint %1506 %2363 %4373 %2365
       %2380 = OpIAdd %uint %1727 %uint_3
       %2386 = OpCompositeConstruct %v2uint %2380 %1734
       %2389 = OpIAdd %v2uint %2386 %1444
       %2391 = OpShiftLeftLogical %v2uint %2389 %932
       %2407 = OpCompositeConstruct %v2uint %4213 %4213
       %2400 = OpShiftRightLogical %v2uint %2407 %733
       %2402 = OpBitwiseAnd %v2uint %2400 %4352
       %2394 = OpIAdd %v2uint %2391 %2402
       %2457 = OpCompositeExtract %uint %2394 0
       %2459 = OpUDiv %uint %2457 %uint_80
       %2461 = OpCompositeExtract %uint %2394 1
       %2463 = OpUDiv %uint %2461 %uint_16
       %2468 = OpIMul %uint %2459 %uint_80
       %2469 = OpISub %uint %2457 %2468
       %2474 = OpIMul %uint %2463 %uint_16
       %2475 = OpISub %uint %2461 %2474
       %2479 = OpIMul %uint %2463 %1827
       %2481 = OpIAdd %uint %2479 %2459
       %2485 = OpIAdd %uint %1832 %2481
       %2489 = OpISub %uint %2485 %1837
       %2494 = OpUDiv %uint %2489 %1840
       %2498 = OpIMul %uint %2494 %1840
       %2499 = OpISub %uint %2489 %2498
       %2502 = OpIMul %uint %2499 %uint_80
       %2504 = OpIAdd %uint %2502 %2469
       %2507 = OpIMul %uint %2494 %uint_16
       %2509 = OpIAdd %uint %2507 %2475
       %2525 = OpBitwiseAnd %uint %2509 %uint_1
       %2526 = OpINotEqual %bool %2525 %uint_0
               OpSelectionMerge %2533 None
               OpBranchConditional %2526 %2527 %2530
       %2530 = OpLabel
       %2531 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2532 = OpLoad %uint %2531
               OpBranch %2533
       %2527 = OpLabel
       %2528 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2529 = OpLoad %uint %2528
               OpBranch %2533
       %2533 = OpLabel
       %4215 = OpPhi %uint %2529 %2527 %2532 %2530
       %2424 = OpBitcast %int %2504
       %2427 = OpShiftRightLogical %uint %2509 %uint_1
       %2428 = OpBitcast %int %2427
       %2432 = OpCompositeConstruct %v2int %2424 %2428
       %2434 = OpBitcast %int %4215
       %2435 = OpImageFetch %v4uint %1770 %2432 Sample %2434
               OpSelectionMerge %2556 None
               OpSwitch %1422 %2541 4 %2544 6 %2544 14 %2553
       %2553 = OpLabel
       %2555 = OpCompositeExtract %uint %2435 0
               OpBranch %2556
       %2544 = OpLabel
       %2546 = OpCompositeExtract %uint %2435 0
       %2547 = OpBitwiseAnd %uint %2546 %uint_65535
       %2549 = OpCompositeExtract %uint %2435 1
       %2550 = OpBitwiseAnd %uint %2549 %uint_65535
       %2551 = OpShiftLeftLogical %uint %2550 %uint_16
       %2552 = OpBitwiseOr %uint %2547 %2551
               OpBranch %2556
       %2541 = OpLabel
       %2543 = OpCompositeExtract %uint %2435 0
               OpBranch %2556
       %2556 = OpLabel
       %4218 = OpPhi %uint %2543 %2541 %2552 %2544 %2555 %2553
       %4365 = OpCompositeConstruct %v4uint %4177 %4194 %4206 %4218
               OpSelectionMerge %2588 None
               OpBranchConditional %1711 %2580 %2582
       %2582 = OpLabel
       %2584 = OpIEqual %bool %1506 %uint_5
       %4374 = OpSelect %uint %2584 %uint_2 %uint_0
               OpBranch %2588
       %2580 = OpLabel
               OpBranch %2588
       %2588 = OpLabel
       %4225 = OpPhi %uint %1506 %2580 %4374 %2582
       %2597 = OpIAdd %uint %1727 %uint_4
       %2603 = OpCompositeConstruct %v2uint %2597 %1734
       %2606 = OpIAdd %v2uint %2603 %1444
       %2608 = OpShiftLeftLogical %v2uint %2606 %932
       %2624 = OpCompositeConstruct %v2uint %4225 %4225
       %2617 = OpShiftRightLogical %v2uint %2624 %733
       %2619 = OpBitwiseAnd %v2uint %2617 %4352
       %2611 = OpIAdd %v2uint %2608 %2619
       %2674 = OpCompositeExtract %uint %2611 0
       %2676 = OpUDiv %uint %2674 %uint_80
       %2678 = OpCompositeExtract %uint %2611 1
       %2680 = OpUDiv %uint %2678 %uint_16
       %2685 = OpIMul %uint %2676 %uint_80
       %2686 = OpISub %uint %2674 %2685
       %2691 = OpIMul %uint %2680 %uint_16
       %2692 = OpISub %uint %2678 %2691
       %2696 = OpIMul %uint %2680 %1827
       %2698 = OpIAdd %uint %2696 %2676
       %2702 = OpIAdd %uint %1832 %2698
       %2706 = OpISub %uint %2702 %1837
       %2711 = OpUDiv %uint %2706 %1840
       %2715 = OpIMul %uint %2711 %1840
       %2716 = OpISub %uint %2706 %2715
       %2719 = OpIMul %uint %2716 %uint_80
       %2721 = OpIAdd %uint %2719 %2686
       %2724 = OpIMul %uint %2711 %uint_16
       %2726 = OpIAdd %uint %2724 %2692
       %2742 = OpBitwiseAnd %uint %2726 %uint_1
       %2743 = OpINotEqual %bool %2742 %uint_0
               OpSelectionMerge %2750 None
               OpBranchConditional %2743 %2744 %2747
       %2747 = OpLabel
       %2748 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2749 = OpLoad %uint %2748
               OpBranch %2750
       %2744 = OpLabel
       %2745 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2746 = OpLoad %uint %2745
               OpBranch %2750
       %2750 = OpLabel
       %4227 = OpPhi %uint %2746 %2744 %2749 %2747
       %2641 = OpBitcast %int %2721
       %2644 = OpShiftRightLogical %uint %2726 %uint_1
       %2645 = OpBitcast %int %2644
       %2649 = OpCompositeConstruct %v2int %2641 %2645
       %2651 = OpBitcast %int %4227
       %2652 = OpImageFetch %v4uint %1770 %2649 Sample %2651
               OpSelectionMerge %2773 None
               OpSwitch %1422 %2758 4 %2761 6 %2761 14 %2770
       %2770 = OpLabel
       %2772 = OpCompositeExtract %uint %2652 0
               OpBranch %2773
       %2761 = OpLabel
       %2763 = OpCompositeExtract %uint %2652 0
       %2764 = OpBitwiseAnd %uint %2763 %uint_65535
       %2766 = OpCompositeExtract %uint %2652 1
       %2767 = OpBitwiseAnd %uint %2766 %uint_65535
       %2768 = OpShiftLeftLogical %uint %2767 %uint_16
       %2769 = OpBitwiseOr %uint %2764 %2768
               OpBranch %2773
       %2758 = OpLabel
       %2760 = OpCompositeExtract %uint %2652 0
               OpBranch %2773
       %2773 = OpLabel
       %4230 = OpPhi %uint %2760 %2758 %2769 %2761 %2772 %2770
               OpSelectionMerge %2805 None
               OpBranchConditional %1711 %2797 %2799
       %2799 = OpLabel
       %2801 = OpIEqual %bool %1506 %uint_5
       %4375 = OpSelect %uint %2801 %uint_2 %uint_0
               OpBranch %2805
       %2797 = OpLabel
               OpBranch %2805
       %2805 = OpLabel
       %4265 = OpPhi %uint %1506 %2797 %4375 %2799
       %2814 = OpIAdd %uint %1727 %uint_5
       %2820 = OpCompositeConstruct %v2uint %2814 %1734
       %2823 = OpIAdd %v2uint %2820 %1444
       %2825 = OpShiftLeftLogical %v2uint %2823 %932
       %2841 = OpCompositeConstruct %v2uint %4265 %4265
       %2834 = OpShiftRightLogical %v2uint %2841 %733
       %2836 = OpBitwiseAnd %v2uint %2834 %4352
       %2828 = OpIAdd %v2uint %2825 %2836
       %2891 = OpCompositeExtract %uint %2828 0
       %2893 = OpUDiv %uint %2891 %uint_80
       %2895 = OpCompositeExtract %uint %2828 1
       %2897 = OpUDiv %uint %2895 %uint_16
       %2902 = OpIMul %uint %2893 %uint_80
       %2903 = OpISub %uint %2891 %2902
       %2908 = OpIMul %uint %2897 %uint_16
       %2909 = OpISub %uint %2895 %2908
       %2913 = OpIMul %uint %2897 %1827
       %2915 = OpIAdd %uint %2913 %2893
       %2919 = OpIAdd %uint %1832 %2915
       %2923 = OpISub %uint %2919 %1837
       %2928 = OpUDiv %uint %2923 %1840
       %2932 = OpIMul %uint %2928 %1840
       %2933 = OpISub %uint %2923 %2932
       %2936 = OpIMul %uint %2933 %uint_80
       %2938 = OpIAdd %uint %2936 %2903
       %2941 = OpIMul %uint %2928 %uint_16
       %2943 = OpIAdd %uint %2941 %2909
       %2959 = OpBitwiseAnd %uint %2943 %uint_1
       %2960 = OpINotEqual %bool %2959 %uint_0
               OpSelectionMerge %2967 None
               OpBranchConditional %2960 %2961 %2964
       %2964 = OpLabel
       %2965 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %2966 = OpLoad %uint %2965
               OpBranch %2967
       %2961 = OpLabel
       %2962 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %2963 = OpLoad %uint %2962
               OpBranch %2967
       %2967 = OpLabel
       %4267 = OpPhi %uint %2963 %2961 %2966 %2964
       %2858 = OpBitcast %int %2938
       %2861 = OpShiftRightLogical %uint %2943 %uint_1
       %2862 = OpBitcast %int %2861
       %2866 = OpCompositeConstruct %v2int %2858 %2862
       %2868 = OpBitcast %int %4267
       %2869 = OpImageFetch %v4uint %1770 %2866 Sample %2868
               OpSelectionMerge %2990 None
               OpSwitch %1422 %2975 4 %2978 6 %2978 14 %2987
       %2987 = OpLabel
       %2989 = OpCompositeExtract %uint %2869 0
               OpBranch %2990
       %2978 = OpLabel
       %2980 = OpCompositeExtract %uint %2869 0
       %2981 = OpBitwiseAnd %uint %2980 %uint_65535
       %2983 = OpCompositeExtract %uint %2869 1
       %2984 = OpBitwiseAnd %uint %2983 %uint_65535
       %2985 = OpShiftLeftLogical %uint %2984 %uint_16
       %2986 = OpBitwiseOr %uint %2981 %2985
               OpBranch %2990
       %2975 = OpLabel
       %2977 = OpCompositeExtract %uint %2869 0
               OpBranch %2990
       %2990 = OpLabel
       %4270 = OpPhi %uint %2977 %2975 %2986 %2978 %2989 %2987
               OpSelectionMerge %3022 None
               OpBranchConditional %1711 %3014 %3016
       %3016 = OpLabel
       %3018 = OpIEqual %bool %1506 %uint_5
       %4376 = OpSelect %uint %3018 %uint_2 %uint_0
               OpBranch %3022
       %3014 = OpLabel
               OpBranch %3022
       %3022 = OpLabel
       %4277 = OpPhi %uint %1506 %3014 %4376 %3016
       %3031 = OpIAdd %uint %1727 %uint_6
       %3037 = OpCompositeConstruct %v2uint %3031 %1734
       %3040 = OpIAdd %v2uint %3037 %1444
       %3042 = OpShiftLeftLogical %v2uint %3040 %932
       %3058 = OpCompositeConstruct %v2uint %4277 %4277
       %3051 = OpShiftRightLogical %v2uint %3058 %733
       %3053 = OpBitwiseAnd %v2uint %3051 %4352
       %3045 = OpIAdd %v2uint %3042 %3053
       %3108 = OpCompositeExtract %uint %3045 0
       %3110 = OpUDiv %uint %3108 %uint_80
       %3112 = OpCompositeExtract %uint %3045 1
       %3114 = OpUDiv %uint %3112 %uint_16
       %3119 = OpIMul %uint %3110 %uint_80
       %3120 = OpISub %uint %3108 %3119
       %3125 = OpIMul %uint %3114 %uint_16
       %3126 = OpISub %uint %3112 %3125
       %3130 = OpIMul %uint %3114 %1827
       %3132 = OpIAdd %uint %3130 %3110
       %3136 = OpIAdd %uint %1832 %3132
       %3140 = OpISub %uint %3136 %1837
       %3145 = OpUDiv %uint %3140 %1840
       %3149 = OpIMul %uint %3145 %1840
       %3150 = OpISub %uint %3140 %3149
       %3153 = OpIMul %uint %3150 %uint_80
       %3155 = OpIAdd %uint %3153 %3120
       %3158 = OpIMul %uint %3145 %uint_16
       %3160 = OpIAdd %uint %3158 %3126
       %3176 = OpBitwiseAnd %uint %3160 %uint_1
       %3177 = OpINotEqual %bool %3176 %uint_0
               OpSelectionMerge %3184 None
               OpBranchConditional %3177 %3178 %3181
       %3181 = OpLabel
       %3182 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %3183 = OpLoad %uint %3182
               OpBranch %3184
       %3178 = OpLabel
       %3179 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %3180 = OpLoad %uint %3179
               OpBranch %3184
       %3184 = OpLabel
       %4279 = OpPhi %uint %3180 %3178 %3183 %3181
       %3075 = OpBitcast %int %3155
       %3078 = OpShiftRightLogical %uint %3160 %uint_1
       %3079 = OpBitcast %int %3078
       %3083 = OpCompositeConstruct %v2int %3075 %3079
       %3085 = OpBitcast %int %4279
       %3086 = OpImageFetch %v4uint %1770 %3083 Sample %3085
               OpSelectionMerge %3207 None
               OpSwitch %1422 %3192 4 %3195 6 %3195 14 %3204
       %3204 = OpLabel
       %3206 = OpCompositeExtract %uint %3086 0
               OpBranch %3207
       %3195 = OpLabel
       %3197 = OpCompositeExtract %uint %3086 0
       %3198 = OpBitwiseAnd %uint %3197 %uint_65535
       %3200 = OpCompositeExtract %uint %3086 1
       %3201 = OpBitwiseAnd %uint %3200 %uint_65535
       %3202 = OpShiftLeftLogical %uint %3201 %uint_16
       %3203 = OpBitwiseOr %uint %3198 %3202
               OpBranch %3207
       %3192 = OpLabel
       %3194 = OpCompositeExtract %uint %3086 0
               OpBranch %3207
       %3207 = OpLabel
       %4282 = OpPhi %uint %3194 %3192 %3203 %3195 %3206 %3204
               OpSelectionMerge %3239 None
               OpBranchConditional %1711 %3231 %3233
       %3233 = OpLabel
       %3235 = OpIEqual %bool %1506 %uint_5
       %4377 = OpSelect %uint %3235 %uint_2 %uint_0
               OpBranch %3239
       %3231 = OpLabel
               OpBranch %3239
       %3239 = OpLabel
       %4289 = OpPhi %uint %1506 %3231 %4377 %3233
       %3248 = OpIAdd %uint %1727 %uint_7
       %3254 = OpCompositeConstruct %v2uint %3248 %1734
       %3257 = OpIAdd %v2uint %3254 %1444
       %3259 = OpShiftLeftLogical %v2uint %3257 %932
       %3275 = OpCompositeConstruct %v2uint %4289 %4289
       %3268 = OpShiftRightLogical %v2uint %3275 %733
       %3270 = OpBitwiseAnd %v2uint %3268 %4352
       %3262 = OpIAdd %v2uint %3259 %3270
       %3325 = OpCompositeExtract %uint %3262 0
       %3327 = OpUDiv %uint %3325 %uint_80
       %3329 = OpCompositeExtract %uint %3262 1
       %3331 = OpUDiv %uint %3329 %uint_16
       %3336 = OpIMul %uint %3327 %uint_80
       %3337 = OpISub %uint %3325 %3336
       %3342 = OpIMul %uint %3331 %uint_16
       %3343 = OpISub %uint %3329 %3342
       %3347 = OpIMul %uint %3331 %1827
       %3349 = OpIAdd %uint %3347 %3327
       %3353 = OpIAdd %uint %1832 %3349
       %3357 = OpISub %uint %3353 %1837
       %3362 = OpUDiv %uint %3357 %1840
       %3366 = OpIMul %uint %3362 %1840
       %3367 = OpISub %uint %3357 %3366
       %3370 = OpIMul %uint %3367 %uint_80
       %3372 = OpIAdd %uint %3370 %3337
       %3375 = OpIMul %uint %3362 %uint_16
       %3377 = OpIAdd %uint %3375 %3343
       %3393 = OpBitwiseAnd %uint %3377 %uint_1
       %3394 = OpINotEqual %bool %3393 %uint_0
               OpSelectionMerge %3401 None
               OpBranchConditional %3394 %3395 %3398
       %3398 = OpLabel
       %3399 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_8
       %3400 = OpLoad %uint %3399
               OpBranch %3401
       %3395 = OpLabel
       %3396 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_9
       %3397 = OpLoad %uint %3396
               OpBranch %3401
       %3401 = OpLabel
       %4291 = OpPhi %uint %3397 %3395 %3400 %3398
       %3292 = OpBitcast %int %3372
       %3295 = OpShiftRightLogical %uint %3377 %uint_1
       %3296 = OpBitcast %int %3295
       %3300 = OpCompositeConstruct %v2int %3292 %3296
       %3302 = OpBitcast %int %4291
       %3303 = OpImageFetch %v4uint %1770 %3300 Sample %3302
               OpSelectionMerge %3424 None
               OpSwitch %1422 %3409 4 %3412 6 %3412 14 %3421
       %3421 = OpLabel
       %3423 = OpCompositeExtract %uint %3303 0
               OpBranch %3424
       %3412 = OpLabel
       %3414 = OpCompositeExtract %uint %3303 0
       %3415 = OpBitwiseAnd %uint %3414 %uint_65535
       %3417 = OpCompositeExtract %uint %3303 1
       %3418 = OpBitwiseAnd %uint %3417 %uint_65535
       %3419 = OpShiftLeftLogical %uint %3418 %uint_16
       %3420 = OpBitwiseOr %uint %3415 %3419
               OpBranch %3424
       %3409 = OpLabel
       %3411 = OpCompositeExtract %uint %3303 0
               OpBranch %3424
       %3424 = OpLabel
       %4294 = OpPhi %uint %3411 %3409 %3420 %3412 %3423 %3421
       %4366 = OpCompositeConstruct %v4uint %4230 %4270 %4282 %4294
       %3432 = OpIEqual %bool %1727 %uint_0
       %4378 = OpSelect %bool %3432 %false %3432
               OpSelectionMerge %3459 DontFlatten
               OpBranchConditional %4378 %3439 %3459
       %3439 = OpLabel
       %4165 = OpCompositeInsert %v4uint %4194 %4365 0
               OpBranch %3459
       %3459 = OpLabel
       %4328 = OpPhi %v4uint %4365 %3424 %4165 %3439
               OpSelectionMerge %3481 DontFlatten
               OpBranchConditional %1483 %3467 %3481
       %3467 = OpLabel
               OpSelectionMerge %3480 None
               OpSwitch %1422 %3480 0 %3470 1 %3470 2 %3475 3 %3475 10 %3475 12 %3475
       %3475 = OpLabel
       %3520 = OpBitwiseAnd %v4uint %4328 %4359
       %3523 = OpBitwiseAnd %v4uint %4328 %4360
       %3525 = OpShiftLeftLogical %v4uint %3523 %4361
       %3526 = OpBitwiseOr %v4uint %3520 %3525
       %3529 = OpShiftRightLogical %v4uint %4328 %4361
       %3531 = OpBitwiseAnd %v4uint %3529 %4360
       %3532 = OpBitwiseOr %v4uint %3526 %3531
       %3537 = OpBitwiseAnd %v4uint %4366 %4359
       %3540 = OpBitwiseAnd %v4uint %4366 %4360
       %3542 = OpShiftLeftLogical %v4uint %3540 %4361
       %3543 = OpBitwiseOr %v4uint %3537 %3542
       %3546 = OpShiftRightLogical %v4uint %4366 %4361
       %3548 = OpBitwiseAnd %v4uint %3546 %4360
       %3549 = OpBitwiseOr %v4uint %3543 %3548
               OpBranch %3480
       %3470 = OpLabel
       %3486 = OpBitwiseAnd %v4uint %4328 %4356
       %3489 = OpBitwiseAnd %v4uint %4328 %4357
       %3491 = OpShiftLeftLogical %v4uint %3489 %4358
       %3492 = OpBitwiseOr %v4uint %3486 %3491
       %3495 = OpShiftRightLogical %v4uint %4328 %4358
       %3497 = OpBitwiseAnd %v4uint %3495 %4357
       %3498 = OpBitwiseOr %v4uint %3492 %3497
       %3503 = OpBitwiseAnd %v4uint %4366 %4356
       %3506 = OpBitwiseAnd %v4uint %4366 %4357
       %3508 = OpShiftLeftLogical %v4uint %3506 %4358
       %3509 = OpBitwiseOr %v4uint %3503 %3508
       %3512 = OpShiftRightLogical %v4uint %4366 %4358
       %3514 = OpBitwiseAnd %v4uint %3512 %4357
       %3515 = OpBitwiseOr %v4uint %3509 %3514
               OpBranch %3480
       %3480 = OpLabel
       %4340 = OpPhi %v4uint %4366 %3467 %3515 %3470 %3549 %3475
       %4338 = OpPhi %v4uint %4328 %3467 %3498 %3470 %3532 %3475
               OpBranch %3481
       %3481 = OpLabel
       %4339 = OpPhi %v4uint %4366 %3459 %4340 %3480
       %4337 = OpPhi %v4uint %4328 %3459 %4338 %3480
       %3556 = OpIAdd %v2uint %4167 %1502
               OpSelectionMerge %3576 DontFlatten
               OpBranchConditional %1462 %3559 %3570
       %3570 = OpLabel
       %3572 = OpBitcast %v2int %3556
       %3661 = OpCompositeExtract %int %3572 1
       %3662 = OpShiftRightArithmetic %int %3661 %int_5
       %3663 = OpBitcast %int %1486
       %3664 = OpIMul %int %3662 %3663
       %3665 = OpCompositeExtract %int %3572 0
       %3666 = OpShiftRightArithmetic %int %3665 %int_5
       %3667 = OpIAdd %int %3664 %3666
       %3668 = OpShiftLeftLogical %int %3667 %int_6
       %3670 = OpShiftRightArithmetic %int %3661 %int_1
       %3671 = OpBitwiseAnd %int %3670 %int_7
       %3672 = OpShiftLeftLogical %int %3671 %int_3
       %3674 = OpBitwiseAnd %int %3665 %int_7
       %3675 = OpBitwiseOr %int %3672 %3674
       %3678 = OpBitwiseOr %int %3668 %3675
       %3679 = OpShiftLeftLogical %int %3678 %uint_2
       %3681 = OpShiftRightArithmetic %int %3661 %int_4
       %3682 = OpBitwiseAnd %int %3681 %int_1
       %3684 = OpShiftRightArithmetic %int %3665 %int_3
       %3685 = OpBitwiseAnd %int %3684 %int_3
       %3687 = OpShiftRightArithmetic %int %3661 %int_3
       %3688 = OpBitwiseAnd %int %3687 %int_1
       %3689 = OpShiftLeftLogical %int %3688 %int_1
       %3690 = OpBitwiseXor %int %3685 %3689
       %3695 = OpBitwiseAnd %int %3661 %int_1
       %3699 = OpShiftLeftLogical %int %3695 %int_4
       %3700 = OpShiftLeftLogical %int %3690 %int_6
       %3701 = OpBitwiseOr %int %3699 %3700
       %3702 = OpShiftLeftLogical %int %3682 %int_11
       %3703 = OpBitwiseOr %int %3701 %3702
       %3704 = OpBitwiseAnd %int %3679 %int_15
       %3705 = OpBitwiseOr %int %3703 %3704
       %3706 = OpShiftRightArithmetic %int %3679 %int_4
       %3707 = OpBitwiseAnd %int %3706 %int_1
       %3708 = OpShiftLeftLogical %int %3707 %int_5
       %3709 = OpBitwiseOr %int %3705 %3708
       %3710 = OpShiftRightArithmetic %int %3679 %int_5
       %3711 = OpBitwiseAnd %int %3710 %int_7
       %3712 = OpShiftLeftLogical %int %3711 %int_8
       %3713 = OpBitwiseOr %int %3709 %3712
       %3714 = OpShiftRightArithmetic %int %3679 %int_8
       %3715 = OpShiftLeftLogical %int %3714 %int_12
       %3716 = OpBitwiseOr %int %3713 %3715
       %3575 = OpBitcast %uint %3716
               OpBranch %3576
       %3559 = OpLabel
       %3562 = OpCompositeExtract %uint %3556 0
       %3563 = OpCompositeExtract %uint %3556 1
       %3564 = OpCompositeConstruct %v3uint %3562 %3563 %1466
       %3565 = OpBitcast %v3int %3564
       %3588 = OpCompositeExtract %int %3565 2
       %3589 = OpShiftRightArithmetic %int %3588 %int_2
       %3590 = OpBitcast %int %1491
       %3591 = OpIMul %int %3589 %3590
       %3592 = OpCompositeExtract %int %3565 1
       %3593 = OpShiftRightArithmetic %int %3592 %int_4
       %3594 = OpIAdd %int %3591 %3593
       %3595 = OpBitcast %int %1486
       %3596 = OpIMul %int %3594 %3595
       %3597 = OpCompositeExtract %int %3565 0
       %3598 = OpShiftRightArithmetic %int %3597 %int_5
       %3599 = OpIAdd %int %3596 %3598
       %3600 = OpShiftLeftLogical %int %3599 %int_7
       %3602 = OpBitwiseAnd %int %3588 %int_3
       %3603 = OpShiftLeftLogical %int %3602 %int_5
       %3605 = OpShiftRightArithmetic %int %3592 %int_1
       %3606 = OpBitwiseAnd %int %3605 %int_3
       %3607 = OpShiftLeftLogical %int %3606 %int_3
       %3608 = OpBitwiseOr %int %3603 %3607
       %3610 = OpBitwiseAnd %int %3597 %int_7
       %3611 = OpBitwiseOr %int %3608 %3610
       %3614 = OpBitwiseOr %int %3600 %3611
       %3615 = OpShiftLeftLogical %int %3614 %uint_2
       %3617 = OpShiftRightArithmetic %int %3592 %int_3
       %3620 = OpBitwiseXor %int %3617 %3589
       %3621 = OpBitwiseAnd %int %3620 %int_1
       %3623 = OpShiftRightArithmetic %int %3597 %int_3
       %3624 = OpBitwiseAnd %int %3623 %int_3
       %3626 = OpShiftLeftLogical %int %3621 %int_1
       %3627 = OpBitwiseXor %int %3624 %3626
       %3632 = OpBitwiseAnd %int %3592 %int_1
       %3636 = OpShiftLeftLogical %int %3632 %int_4
       %3637 = OpShiftLeftLogical %int %3627 %int_6
       %3638 = OpBitwiseOr %int %3636 %3637
       %3639 = OpShiftLeftLogical %int %3621 %int_11
       %3640 = OpBitwiseOr %int %3638 %3639
       %3641 = OpBitwiseAnd %int %3615 %int_15
       %3642 = OpBitwiseOr %int %3640 %3641
       %3643 = OpShiftRightArithmetic %int %3615 %int_4
       %3644 = OpBitwiseAnd %int %3643 %int_1
       %3645 = OpShiftLeftLogical %int %3644 %int_5
       %3646 = OpBitwiseOr %int %3642 %3645
       %3647 = OpShiftRightArithmetic %int %3615 %int_5
       %3648 = OpBitwiseAnd %int %3647 %int_7
       %3649 = OpShiftLeftLogical %int %3648 %int_8
       %3650 = OpBitwiseOr %int %3646 %3649
       %3651 = OpShiftRightArithmetic %int %3615 %int_8
       %3652 = OpShiftLeftLogical %int %3651 %int_12
       %3653 = OpBitwiseOr %int %3650 %3652
       %3569 = OpBitcast %uint %3653
               OpBranch %3576
       %3576 = OpLabel
       %4341 = OpPhi %uint %3569 %3559 %3575 %3570
       %3579 = OpIAdd %uint %4341 %1509
       %1372 = OpShiftRightLogical %uint %3579 %int_4
       %3720 = OpIEqual %bool %1458 %uint_1
       %3722 = OpIEqual %bool %1458 %uint_2
       %3723 = OpLogicalOr %bool %3720 %3722
               OpSelectionMerge %3736 None
               OpBranchConditional %3723 %3724 %3736
       %3724 = OpLabel
       %3727 = OpBitwiseAnd %v4uint %4337 %4362
       %3729 = OpShiftLeftLogical %v4uint %3727 %4363
       %3732 = OpBitwiseAnd %v4uint %4337 %4356
       %3734 = OpShiftRightLogical %v4uint %3732 %4363
       %3735 = OpBitwiseOr %v4uint %3729 %3734
               OpBranch %3736
       %3736 = OpLabel
       %4343 = OpPhi %v4uint %4337 %3576 %3735 %3724
       %3740 = OpIEqual %bool %1458 %uint_3
       %3741 = OpLogicalOr %bool %3722 %3740
               OpSelectionMerge %3750 None
               OpBranchConditional %3741 %3742 %3750
       %3742 = OpLabel
       %3745 = OpShiftLeftLogical %v4uint %4343 %4358
       %3748 = OpShiftRightLogical %v4uint %4343 %4358
       %3749 = OpBitwiseOr %v4uint %3745 %3748
               OpBranch %3750
       %3750 = OpLabel
       %4344 = OpPhi %v4uint %4343 %3736 %3749 %3742
       %1377 = OpAccessChain %_ptr_StorageBuffer_v4uint %xe_resolve_dest %int_0 %1372
               OpStore %1377 %4344
       %1380 = OpIAdd %uint %3579 %uint_32
       %1382 = OpShiftRightLogical %uint %1380 %int_4
               OpSelectionMerge %3803 None
               OpBranchConditional %3723 %3791 %3803
       %3791 = OpLabel
       %3794 = OpBitwiseAnd %v4uint %4339 %4362
       %3796 = OpShiftLeftLogical %v4uint %3794 %4363
       %3799 = OpBitwiseAnd %v4uint %4339 %4356
       %3801 = OpShiftRightLogical %v4uint %3799 %4363
       %3802 = OpBitwiseOr %v4uint %3796 %3801
               OpBranch %3803
       %3803 = OpLabel
       %4350 = OpPhi %v4uint %4339 %3750 %3802 %3791
               OpSelectionMerge %3817 None
               OpBranchConditional %3741 %3809 %3817
       %3809 = OpLabel
       %3812 = OpShiftLeftLogical %v4uint %4350 %4358
       %3815 = OpShiftRightLogical %v4uint %4350 %4358
       %3816 = OpBitwiseOr %v4uint %3812 %3815
               OpBranch %3817
       %3817 = OpLabel
       %4351 = OpPhi %v4uint %4350 %3803 %3816 %3809
       %1387 = OpAccessChain %_ptr_StorageBuffer_v4uint %xe_resolve_dest %int_0 %1382
               OpStore %1387 %4351
               OpBranch %1388
       %1388 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t resolve_host_color_uint_32bpp_2xmsaa_cs[] = {
    0x07230203, 0x00010300, 0x000D000A, 0x0000111B, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x00000004, 0x6E69616D, 0x00000000, 0x0000049A, 0x00060010, 0x00000004,
    0x00000011, 0x00000008, 0x00000008, 0x00000001, 0x00030003, 0x00000002,
    0x000001CC, 0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F,
    0x5F776F6C, 0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47,
    0x735F5458, 0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572,
    0x74636E75, 0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6E69616D,
    0x00000000, 0x00070005, 0x0000014F, 0x68737570, 0x6E6F635F, 0x625F7473,
    0x6B636F6C, 0x0065785F, 0x00090006, 0x0000014F, 0x00000000, 0x725F6578,
    0x6C6F7365, 0x655F6576, 0x6D617264, 0x666E695F, 0x0000006F, 0x000A0006,
    0x0000014F, 0x00000001, 0x725F6578, 0x6C6F7365, 0x635F6576, 0x64726F6F,
    0x74616E69, 0x6E695F65, 0x00006F66, 0x00090006, 0x0000014F, 0x00000002,
    0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365, 0x6F666E69, 0x00000000,
    0x000B0006, 0x0000014F, 0x00000003, 0x725F6578, 0x6C6F7365, 0x645F6576,
    0x5F747365, 0x726F6F63, 0x616E6964, 0x695F6574, 0x006F666E, 0x00090006,
    0x0000014F, 0x00000004, 0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365,
    0x65736162, 0x00000000, 0x00060005, 0x00000151, 0x68737570, 0x6E6F635F,
    0x5F737473, 0x00006578, 0x000A0005, 0x000002AD, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x65785F72, 0x6F6C625F, 0x00006B63,
    0x000D0006, 0x000002AD, 0x00000000, 0x725F6578, 0x6C6F7365, 0x685F6576,
    0x5F74736F, 0x6F6C6F63, 0x69645F72, 0x74617073, 0x6F5F6863, 0x65736666,
    0x00000074, 0x000B0006, 0x000002AD, 0x00000001, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x75645F72, 0x625F706D, 0x00657361,
    0x000D0006, 0x000002AD, 0x00000002, 0x725F6578, 0x6C6F7365, 0x685F6576,
    0x5F74736F, 0x6F6C6F63, 0x75645F72, 0x705F706D, 0x68637469, 0x6C69745F,
    0x00007365, 0x000D0006, 0x000002AD, 0x00000003, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x6F735F72, 0x65637275, 0x7361625F,
    0x69745F65, 0x0073656C, 0x000E0006, 0x000002AD, 0x00000004, 0x725F6578,
    0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x6F735F72, 0x65637275,
    0x7469705F, 0x745F6863, 0x73656C69, 0x00000000, 0x000D0006, 0x000002AD,
    0x00000005, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x68745F72, 0x64616572, 0x756F635F, 0x785F746E, 0x00000000, 0x000D0006,
    0x000002AD, 0x00000006, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F,
    0x6F6C6F63, 0x68745F72, 0x64616572, 0x756F635F, 0x795F746E, 0x00000000,
    0x000C0006, 0x000002AD, 0x00000007, 0x725F6578, 0x6C6F7365, 0x685F6576,
    0x5F74736F, 0x6F6C6F63, 0x65685F72, 0x74686769, 0x6163735F, 0x0064656C,
    0x000D0006, 0x000002AD, 0x00000008, 0x725F6578, 0x6C6F7365, 0x685F6576,
    0x5F74736F, 0x6F6C6F63, 0x736D5F72, 0x325F6161, 0x61735F78, 0x656C706D,
    0x0000305F, 0x000D0006, 0x000002AD, 0x00000009, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x736D5F72, 0x325F6161, 0x61735F78,
    0x656C706D, 0x0000315F, 0x000A0006, 0x000002AD, 0x0000000A, 0x725F6578,
    0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x6C665F72, 0x00736761,
    0x00080005, 0x000002AF, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F,
    0x6F6C6F63, 0x00000072, 0x000A0005, 0x000002FE, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x6F735F72, 0x65637275, 0x00000000,
    0x00090005, 0x00000480, 0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365,
    0x625F6578, 0x6B636F6C, 0x00000000, 0x00050006, 0x00000480, 0x00000000,
    0x61746164, 0x00000000, 0x00060005, 0x00000482, 0x725F6578, 0x6C6F7365,
    0x645F6576, 0x00747365, 0x00080005, 0x0000049A, 0x475F6C67, 0x61626F6C,
    0x766E496C, 0x7461636F, 0x496E6F69, 0x00000044, 0x00050048, 0x0000014F,
    0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x0000014F, 0x00000001,
    0x00000023, 0x00000004, 0x00050048, 0x0000014F, 0x00000002, 0x00000023,
    0x00000008, 0x00050048, 0x0000014F, 0x00000003, 0x00000023, 0x0000000C,
    0x00050048, 0x0000014F, 0x00000004, 0x00000023, 0x00000010, 0x00030047,
    0x0000014F, 0x00000002, 0x00050048, 0x000002AD, 0x00000000, 0x00000023,
    0x00000000, 0x00050048, 0x000002AD, 0x00000001, 0x00000023, 0x00000004,
    0x00050048, 0x000002AD, 0x00000002, 0x00000023, 0x00000008, 0x00050048,
    0x000002AD, 0x00000003, 0x00000023, 0x0000000C, 0x00050048, 0x000002AD,
    0x00000004, 0x00000023, 0x00000010, 0x00050048, 0x000002AD, 0x00000005,
    0x00000023, 0x00000014, 0x00050048, 0x000002AD, 0x00000006, 0x00000023,
    0x00000018, 0x00050048, 0x000002AD, 0x00000007, 0x00000023, 0x0000001C,
    0x00050048, 0x000002AD, 0x00000008, 0x00000023, 0x00000020, 0x00050048,
    0x000002AD, 0x00000009, 0x00000023, 0x00000024, 0x00050048, 0x000002AD,
    0x0000000A, 0x00000023, 0x00000028, 0x00030047, 0x000002AD, 0x00000002,
    0x00040047, 0x000002AF, 0x00000022, 0x00000000, 0x00040047, 0x000002AF,
    0x00000021, 0x00000001, 0x00040047, 0x000002FE, 0x00000022, 0x00000002,
    0x00040047, 0x000002FE, 0x00000021, 0x00000000, 0x00040047, 0x0000047F,
    0x00000006, 0x00000010, 0x00040048, 0x00000480, 0x00000000, 0x00000019,
    0x00050048, 0x00000480, 0x00000000, 0x00000023, 0x00000000, 0x00030047,
    0x00000480, 0x00000002, 0x00040047, 0x00000482, 0x00000022, 0x00000001,
    0x00040047, 0x00000482, 0x00000021, 0x00000000, 0x00040047, 0x0000049A,
    0x0000000B, 0x0000001C, 0x00040047, 0x0000049F, 0x0000000B, 0x00000019,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015,
    0x00000006, 0x00000020, 0x00000000, 0x00040017, 0x00000008, 0x00000006,
    0x00000002, 0x00040017, 0x0000000D, 0x00000006, 0x00000004, 0x00040015,
    0x00000014, 0x00000020, 0x00000001, 0x00040017, 0x0000001C, 0x00000014,
    0x00000002, 0x00040017, 0x00000023, 0x00000014, 0x00000003, 0x00020014,
    0x00000030, 0x0004002B, 0x00000006, 0x0000009C, 0x00000001, 0x0004002B,
    0x00000006, 0x0000009F, 0x00000002, 0x0004002B, 0x00000006, 0x000000A5,
    0x00FF00FF, 0x0004002B, 0x00000006, 0x000000A8, 0x00000008, 0x0004002B,
    0x00000006, 0x000000AC, 0xFF00FF00, 0x0004002B, 0x00000006, 0x000000B5,
    0x00000003, 0x0004002B, 0x00000006, 0x000000BB, 0x00000010, 0x0004002B,
    0x00000014, 0x000000C5, 0x00000004, 0x0004002B, 0x00000014, 0x000000C7,
    0x00000006, 0x0004002B, 0x00000014, 0x000000CA, 0x0000000B, 0x0004002B,
    0x00000014, 0x000000CD, 0x0000000F, 0x0004002B, 0x00000014, 0x000000D1,
    0x00000001, 0x0004002B, 0x00000014, 0x000000D3, 0x00000005, 0x0004002B,
    0x00000014, 0x000000D7, 0x00000007, 0x0004002B, 0x00000014, 0x000000D9,
    0x00000008, 0x0004002B, 0x00000014, 0x000000DD, 0x0000000C, 0x0004002B,
    0x00000006, 0x000000E8, 0x00000000, 0x0004002B, 0x00000014, 0x000000F1,
    0x00000003, 0x0004002B, 0x00000014, 0x00000112, 0x00000002, 0x0004002B,
    0x00000014, 0x00000148, 0x00000000, 0x0007001E, 0x0000014F, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00040020, 0x00000150,
    0x00000009, 0x0000014F, 0x0004003B, 0x00000150, 0x00000151, 0x00000009,
    0x00040020, 0x00000152, 0x00000009, 0x00000006, 0x0004002B, 0x00000006,
    0x0000015A, 0x000003FF, 0x0004002B, 0x00000006, 0x0000015E, 0x0000000A,
    0x0004002B, 0x00000006, 0x0000016B, 0x000007FF, 0x0004002B, 0x00000006,
    0x0000016F, 0x00000018, 0x0004002B, 0x00000006, 0x00000171, 0x0000000F,
    0x0004002B, 0x00000006, 0x00000175, 0x0000001C, 0x0004002B, 0x00000006,
    0x00000182, 0x00000004, 0x0005002C, 0x00000008, 0x00000183, 0x000000E8,
    0x00000182, 0x0005002C, 0x00000008, 0x00000187, 0x00000182, 0x0000009C,
    0x0004002B, 0x00000014, 0x00000192, 0x00000009, 0x0004002B, 0x00000006,
    0x00000194, 0x00000005, 0x0004002B, 0x00000006, 0x000001A3, 0x00000007,
    0x0004002B, 0x00000006, 0x000001C3, 0x01000000, 0x0004002B, 0x00000006,
    0x000001D5, 0x00000014, 0x0005002C, 0x00000008, 0x000001D6, 0x000001D5,
    0x0000016F, 0x00040017, 0x000001F7, 0x00000006, 0x00000003, 0x0004002B,
    0x00000006, 0x00000224, 0x000000FF, 0x0004002B, 0x00000006, 0x00000233,
    0xC00FFC00, 0x0004002B, 0x00000006, 0x0000025C, 0x00000050, 0x0004002B,
    0x00000006, 0x0000027B, 0x0000FFFF, 0x000D001E, 0x000002AD, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00040020, 0x000002AE,
    0x00000002, 0x000002AD, 0x0004003B, 0x000002AE, 0x000002AF, 0x00000002,
    0x00040020, 0x000002B0, 0x00000002, 0x00000006, 0x0005002C, 0x00000008,
    0x000002DD, 0x0000009C, 0x000000E8, 0x00090019, 0x000002FC, 0x00000006,
    0x00000001, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000000,
    0x00040020, 0x000002FD, 0x00000000, 0x000002FC, 0x0004003B, 0x000002FD,
    0x000002FE, 0x00000000, 0x0003002A, 0x00000030, 0x00000320, 0x00030029,
    0x00000030, 0x00000392, 0x0005002C, 0x00000008, 0x000003A4, 0x000000E8,
    0x0000009C, 0x0004002B, 0x00000006, 0x00000448, 0x00000006, 0x0003001D,
    0x0000047F, 0x0000000D, 0x0003001E, 0x00000480, 0x0000047F, 0x00040020,
    0x00000481, 0x0000000C, 0x00000480, 0x0004003B, 0x00000481, 0x00000482,
    0x0000000C, 0x00040020, 0x0000048B, 0x0000000C, 0x0000000D, 0x00040020,
    0x00000499, 0x00000001, 0x000001F7, 0x0004003B, 0x00000499, 0x0000049A,
    0x00000001, 0x0006002C, 0x000001F7, 0x0000049F, 0x000000A8, 0x000000A8,
    0x0000009C, 0x00030001, 0x00000008, 0x00001049, 0x0005002C, 0x00000008,
    0x00001100, 0x0000009C, 0x0000009C, 0x0005002C, 0x00000008, 0x00001102,
    0x000000B5, 0x000000B5, 0x0005002C, 0x00000008, 0x00001103, 0x00000171,
    0x00000171, 0x0007002C, 0x0000000D, 0x00001104, 0x000000AC, 0x000000AC,
    0x000000AC, 0x000000AC, 0x0007002C, 0x0000000D, 0x00001105, 0x00000224,
    0x00000224, 0x00000224, 0x00000224, 0x0007002C, 0x0000000D, 0x00001106,
    0x000000BB, 0x000000BB, 0x000000BB, 0x000000BB, 0x0007002C, 0x0000000D,
    0x00001107, 0x00000233, 0x00000233, 0x00000233, 0x00000233, 0x0007002C,
    0x0000000D, 0x00001108, 0x0000015A, 0x0000015A, 0x0000015A, 0x0000015A,
    0x0007002C, 0x0000000D, 0x00001109, 0x000001D5, 0x000001D5, 0x000001D5,
    0x000001D5, 0x0007002C, 0x0000000D, 0x0000110A, 0x000000A5, 0x000000A5,
    0x000000A5, 0x000000A5, 0x0007002C, 0x0000000D, 0x0000110B, 0x000000A8,
    0x000000A8, 0x000000A8, 0x000000A8, 0x0004002B, 0x00000006, 0x00001111,
    0x00000020, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200F8, 0x00000005, 0x0004003D, 0x000001F7, 0x0000049C, 0x0000049A,
    0x000300F7, 0x0000056C, 0x00000000, 0x000300FB, 0x000000E8, 0x000004FC,
    0x000200F8, 0x000004FC, 0x00050041, 0x00000152, 0x00000579, 0x00000151,
    0x00000148, 0x0004003D, 0x00000006, 0x0000057A, 0x00000579, 0x00050041,
    0x00000152, 0x0000057B, 0x00000151, 0x000000D1, 0x0004003D, 0x00000006,
    0x0000057C, 0x0000057B, 0x000500C2, 0x00000006, 0x0000058D, 0x0000057A,
    0x0000016F, 0x000500C7, 0x00000006, 0x0000058E, 0x0000058D, 0x00000171,
    0x00050050, 0x00000008, 0x000005F6, 0x0000057C, 0x0000057C, 0x000500C2,
    0x00000008, 0x0000059A, 0x000005F6, 0x00000183, 0x000500C4, 0x00000008,
    0x0000059C, 0x00001100, 0x00000187, 0x00050082, 0x00000008, 0x0000059E,
    0x0000059C, 0x00001100, 0x000500C7, 0x00000008, 0x0000059F, 0x0000059A,
    0x0000059E, 0x000500C4, 0x00000008, 0x000005A1, 0x0000059F, 0x00001102,
    0x00050084, 0x00000008, 0x000005A4, 0x000005A1, 0x00001100, 0x000500C2,
    0x00000006, 0x000005A7, 0x0000057C, 0x00000194, 0x000500C7, 0x00000006,
    0x000005A8, 0x000005A7, 0x0000016B, 0x00050041, 0x00000152, 0x000005AD,
    0x00000151, 0x00000112, 0x0004003D, 0x00000006, 0x000005AE, 0x000005AD,
    0x00050041, 0x00000152, 0x000005AF, 0x00000151, 0x000000F1, 0x0004003D,
    0x00000006, 0x000005B0, 0x000005AF, 0x000500C7, 0x00000006, 0x000005B2,
    0x000005AE, 0x000001A3, 0x000500C7, 0x00000006, 0x000005B5, 0x000005AE,
    0x000000A8, 0x000500AB, 0x00000030, 0x000005B6, 0x000005B5, 0x000000E8,
    0x000500C2, 0x00000006, 0x000005B9, 0x000005AE, 0x00000182, 0x000500C7,
    0x00000006, 0x000005BA, 0x000005B9, 0x000001A3, 0x000500C7, 0x00000006,
    0x000005CA, 0x000005AE, 0x000001C3, 0x000500AB, 0x00000030, 0x000005CB,
    0x000005CA, 0x000000E8, 0x000500C7, 0x00000006, 0x000005CE, 0x000005B0,
    0x0000015A, 0x000500C2, 0x00000006, 0x000005D1, 0x000005B0, 0x0000015E,
    0x000500C7, 0x00000006, 0x000005D2, 0x000005D1, 0x0000015A, 0x000500C4,
    0x00000006, 0x000005D3, 0x000005D2, 0x000000D1, 0x00050050, 0x00000008,
    0x00000600, 0x000005B0, 0x000005B0, 0x000500C2, 0x00000008, 0x000005D7,
    0x00000600, 0x000001D6, 0x000500C7, 0x00000008, 0x000005D9, 0x000005D7,
    0x00001103, 0x000500C4, 0x00000008, 0x000005DB, 0x000005D9, 0x00001102,
    0x00050084, 0x00000008, 0x000005DE, 0x000005DB, 0x00001100, 0x000500C2,
    0x00000006, 0x000005E1, 0x000005B0, 0x00000175, 0x000500C7, 0x00000006,
    0x000005E2, 0x000005E1, 0x000001A3, 0x00050041, 0x00000152, 0x000005E4,
    0x00000151, 0x000000C5, 0x0004003D, 0x00000006, 0x000005E5, 0x000005E4,
    0x000300F7, 0x00000684, 0x00000000, 0x000300FB, 0x000000E8, 0x00000615,
    0x000200F8, 0x00000615, 0x00050051, 0x00000006, 0x00000617, 0x0000049C,
    0x00000000, 0x00050041, 0x000002B0, 0x00000618, 0x000002AF, 0x000000D3,
    0x0004003D, 0x00000006, 0x00000619, 0x00000618, 0x000500AE, 0x00000030,
    0x0000061A, 0x00000617, 0x00000619, 0x000400A8, 0x00000030, 0x0000061B,
    0x0000061A, 0x000300F7, 0x00000622, 0x00000000, 0x000400FA, 0x0000061B,
    0x0000061C, 0x00000622, 0x000200F8, 0x0000061C, 0x00050051, 0x00000006,
    0x0000061E, 0x0000049C, 0x00000001, 0x00050041, 0x000002B0, 0x0000061F,
    0x000002AF, 0x000000C7, 0x0004003D, 0x00000006, 0x00000620, 0x0000061F,
    0x000500AE, 0x00000030, 0x00000621, 0x0000061E, 0x00000620, 0x000200F9,
    0x00000622, 0x000200F8, 0x00000622, 0x000700F5, 0x00000030, 0x00000623,
    0x0000061A, 0x00000615, 0x00000621, 0x0000061C, 0x000300F7, 0x00000625,
    0x00000000, 0x000400FA, 0x00000623, 0x00000624, 0x00000625, 0x000200F8,
    0x00000624, 0x000200F9, 0x00000684, 0x000200F8, 0x00000625, 0x00050084,
    0x00000006, 0x0000062E, 0x00000617, 0x000000A8, 0x00050051, 0x00000006,
    0x00000630, 0x0000049C, 0x00000001, 0x00050086, 0x00000006, 0x00000633,
    0x0000062E, 0x0000025C, 0x00050086, 0x00000006, 0x00000636, 0x00000630,
    0x000000A8, 0x00050084, 0x00000006, 0x0000063A, 0x00000633, 0x0000025C,
    0x00050082, 0x00000006, 0x0000063B, 0x0000062E, 0x0000063A, 0x00050084,
    0x00000006, 0x0000063F, 0x00000636, 0x000000A8, 0x00050082, 0x00000006,
    0x00000640, 0x00000630, 0x0000063F, 0x00050041, 0x000002B0, 0x00000641,
    0x000002AF, 0x00000148, 0x0004003D, 0x00000006, 0x00000642, 0x00000641,
    0x00050041, 0x000002B0, 0x00000644, 0x000002AF, 0x00000112, 0x0004003D,
    0x00000006, 0x00000645, 0x00000644, 0x00050084, 0x00000006, 0x00000646,
    0x00000636, 0x00000645, 0x00050080, 0x00000006, 0x00000647, 0x00000642,
    0x00000646, 0x00050080, 0x00000006, 0x00000649, 0x00000647, 0x00000633,
    0x00050086, 0x00000006, 0x0000064E, 0x00000649, 0x00000645, 0x00050084,
    0x00000006, 0x00000652, 0x0000064E, 0x00000645, 0x00050082, 0x00000006,
    0x00000653, 0x00000649, 0x00000652, 0x00050084, 0x00000006, 0x00000656,
    0x00000653, 0x0000025C, 0x00050080, 0x00000006, 0x00000658, 0x00000656,
    0x0000063B, 0x00050084, 0x00000006, 0x0000065B, 0x0000064E, 0x000000A8,
    0x00050080, 0x00000006, 0x0000065D, 0x0000065B, 0x00000640, 0x00050050,
    0x00000008, 0x0000065E, 0x00000658, 0x0000065D, 0x00050051, 0x00000006,
    0x00000662, 0x000005A4, 0x00000000, 0x000500B0, 0x00000030, 0x00000663,
    0x00000658, 0x00000662, 0x000400A8, 0x00000030, 0x00000664, 0x00000663,
    0x000300F7, 0x0000066B, 0x00000000, 0x000400FA, 0x00000664, 0x00000665,
    0x0000066B, 0x000200F8, 0x00000665, 0x00050051, 0x00000006, 0x00000669,
    0x000005A4, 0x00000001, 0x000500B0, 0x00000030, 0x0000066A, 0x0000065D,
    0x00000669, 0x000200F9, 0x0000066B, 0x000200F8, 0x0000066B, 0x000700F5,
    0x00000030, 0x0000066C, 0x00000663, 0x00000625, 0x0000066A, 0x00000665,
    0x000300F7, 0x0000066E, 0x00000000, 0x000400FA, 0x0000066C, 0x0000066D,
    0x0000066E, 0x000200F8, 0x0000066D, 0x000200F9, 0x00000684, 0x000200F8,
    0x0000066E, 0x00050082, 0x00000008, 0x00000672, 0x0000065E, 0x000005A4,
    0x00050051, 0x00000006, 0x00000674, 0x00000672, 0x00000000, 0x000500C4,
    0x00000006, 0x00000677, 0x000005A8, 0x000000B5, 0x000500AE, 0x00000030,
    0x00000678, 0x00000674, 0x00000677, 0x000400A8, 0x00000030, 0x00000679,
    0x00000678, 0x000300F7, 0x00000680, 0x00000000, 0x000400FA, 0x00000679,
    0x0000067A, 0x00000680, 0x000200F8, 0x0000067A, 0x00050051, 0x00000006,
    0x0000067C, 0x00000672, 0x00000001, 0x00050041, 0x000002B0, 0x0000067D,
    0x000002AF, 0x000000D7, 0x0004003D, 0x00000006, 0x0000067E, 0x0000067D,
    0x000500AE, 0x00000030, 0x0000067F, 0x0000067C, 0x0000067E, 0x000200F9,
    0x00000680, 0x000200F8, 0x00000680, 0x000700F5, 0x00000030, 0x00000681,
    0x00000678, 0x0000066E, 0x0000067F, 0x0000067A, 0x000300F7, 0x00000683,
    0x00000000, 0x000400FA, 0x00000681, 0x00000682, 0x00000683, 0x000200F8,
    0x00000682, 0x000200F9, 0x00000684, 0x000200F8, 0x00000683, 0x000200F9,
    0x00000684, 0x000200F8, 0x00000684, 0x000B00F5, 0x00000008, 0x00001047,
    0x00001049, 0x00000624, 0x00001049, 0x0000066D, 0x00000672, 0x00000682,
    0x00000672, 0x00000683, 0x000B00F5, 0x00000030, 0x00001046, 0x00000320,
    0x00000624, 0x00000320, 0x0000066D, 0x00000320, 0x00000682, 0x00000392,
    0x00000683, 0x000400A8, 0x00000030, 0x00000502, 0x00001046, 0x000300F7,
    0x00000504, 0x00000000, 0x000400FA, 0x00000502, 0x00000503, 0x00000504,
    0x000200F8, 0x00000503, 0x000200F9, 0x0000056C, 0x000200F8, 0x00000504,
    0x000500B2, 0x00000030, 0x000006AF, 0x000005E2, 0x000000B5, 0x000300F7,
    0x000006B8, 0x00000000, 0x000400FA, 0x000006AF, 0x000006B0, 0x000006B2,
    0x000200F8, 0x000006B2, 0x000500AA, 0x00000030, 0x000006B4, 0x000005E2,
    0x00000194, 0x000600A9, 0x00000006, 0x00001112, 0x000006B4, 0x0000009F,
    0x000000E8, 0x000200F9, 0x000006B8, 0x000200F8, 0x000006B0, 0x000200F9,
    0x000006B8, 0x000200F8, 0x000006B8, 0x000700F5, 0x00000006, 0x0000104C,
    0x000005E2, 0x000006B0, 0x00001112, 0x000006B2, 0x00050051, 0x00000006,
    0x000006BF, 0x00001047, 0x00000000, 0x00050051, 0x00000006, 0x000006C3,
    0x00001047, 0x00000001, 0x0007000C, 0x00000006, 0x000006C6, 0x00000001,
    0x00000029, 0x000006C3, 0x000000E8, 0x00050050, 0x00000008, 0x000006C7,
    0x000006BF, 0x000006C6, 0x00050080, 0x00000008, 0x000006CA, 0x000006C7,
    0x000005A4, 0x000500C4, 0x00000008, 0x000006CC, 0x000006CA, 0x000003A4,
    0x00050050, 0x00000008, 0x000006DC, 0x0000104C, 0x0000104C, 0x000500C2,
    0x00000008, 0x000006D5, 0x000006DC, 0x000002DD, 0x000500C7, 0x00000008,
    0x000006D7, 0x000006D5, 0x00001100, 0x00050080, 0x00000008, 0x000006CF,
    0x000006CC, 0x000006D7, 0x00050051, 0x00000006, 0x0000070E, 0x000006CF,
    0x00000000, 0x00050086, 0x00000006, 0x00000710, 0x0000070E, 0x0000025C,
    0x00050051, 0x00000006, 0x00000712, 0x000006CF, 0x00000001, 0x00050086,
    0x00000006, 0x00000714, 0x00000712, 0x000000BB, 0x00050084, 0x00000006,
    0x00000719, 0x00000710, 0x0000025C, 0x00050082, 0x00000006, 0x0000071A,
    0x0000070E, 0x00000719, 0x00050084, 0x00000006, 0x0000071F, 0x00000714,
    0x000000BB, 0x00050082, 0x00000006, 0x00000720, 0x00000712, 0x0000071F,
    0x00050041, 0x000002B0, 0x00000722, 0x000002AF, 0x00000112, 0x0004003D,
    0x00000006, 0x00000723, 0x00000722, 0x00050084, 0x00000006, 0x00000724,
    0x00000714, 0x00000723, 0x00050080, 0x00000006, 0x00000726, 0x00000724,
    0x00000710, 0x00050041, 0x000002B0, 0x00000727, 0x000002AF, 0x000000D1,
    0x0004003D, 0x00000006, 0x00000728, 0x00000727, 0x00050080, 0x00000006,
    0x0000072A, 0x00000728, 0x00000726, 0x00050041, 0x000002B0, 0x0000072C,
    0x000002AF, 0x000000F1, 0x0004003D, 0x00000006, 0x0000072D, 0x0000072C,
    0x00050082, 0x00000006, 0x0000072E, 0x0000072A, 0x0000072D, 0x00050041,
    0x000002B0, 0x0000072F, 0x000002AF, 0x000000C5, 0x0004003D, 0x00000006,
    0x00000730, 0x0000072F, 0x00050086, 0x00000006, 0x00000733, 0x0000072E,
    0x00000730, 0x00050084, 0x00000006, 0x00000737, 0x00000733, 0x00000730,
    0x00050082, 0x00000006, 0x00000738, 0x0000072E, 0x00000737, 0x00050084,
    0x00000006, 0x0000073B, 0x00000738, 0x0000025C, 0x00050080, 0x00000006,
    0x0000073D, 0x0000073B, 0x0000071A, 0x00050084, 0x00000006, 0x00000740,
    0x00000733, 0x000000BB, 0x00050080, 0x00000006, 0x00000742, 0x00000740,
    0x00000720, 0x000500C7, 0x00000006, 0x00000752, 0x00000742, 0x0000009C,
    0x000500AB, 0x00000030, 0x00000753, 0x00000752, 0x000000E8, 0x000300F7,
    0x0000075A, 0x00000000, 0x000400FA, 0x00000753, 0x00000754, 0x00000757,
    0x000200F8, 0x00000757, 0x00050041, 0x000002B0, 0x00000758, 0x000002AF,
    0x000000D9, 0x0004003D, 0x00000006, 0x00000759, 0x00000758, 0x000200F9,
    0x0000075A, 0x000200F8, 0x00000754, 0x00050041, 0x000002B0, 0x00000755,
    0x000002AF, 0x00000192, 0x0004003D, 0x00000006, 0x00000756, 0x00000755,
    0x000200F9, 0x0000075A, 0x000200F8, 0x0000075A, 0x000700F5, 0x00000006,
    0x0000104E, 0x00000756, 0x00000754, 0x00000759, 0x00000757, 0x0004003D,
    0x000002FC, 0x000006EA, 0x000002FE, 0x0004007C, 0x00000014, 0x000006ED,
    0x0000073D, 0x000500C2, 0x00000006, 0x000006F0, 0x00000742, 0x0000009C,
    0x0004007C, 0x00000014, 0x000006F1, 0x000006F0, 0x00050050, 0x0000001C,
    0x000006F5, 0x000006ED, 0x000006F1, 0x0004007C, 0x00000014, 0x000006F7,
    0x0000104E, 0x0007005F, 0x0000000D, 0x000006F8, 0x000006EA, 0x000006F5,
    0x00000040, 0x000006F7, 0x000300F7, 0x00000771, 0x00000000, 0x000900FB,
    0x0000058E, 0x00000762, 0x00000004, 0x00000765, 0x00000006, 0x00000765,
    0x0000000E, 0x0000076E, 0x000200F8, 0x0000076E, 0x00050051, 0x00000006,
    0x00000770, 0x000006F8, 0x00000000, 0x000200F9, 0x00000771, 0x000200F8,
    0x00000765, 0x00050051, 0x00000006, 0x00000767, 0x000006F8, 0x00000000,
    0x000500C7, 0x00000006, 0x00000768, 0x00000767, 0x0000027B, 0x00050051,
    0x00000006, 0x0000076A, 0x000006F8, 0x00000001, 0x000500C7, 0x00000006,
    0x0000076B, 0x0000076A, 0x0000027B, 0x000500C4, 0x00000006, 0x0000076C,
    0x0000076B, 0x000000BB, 0x000500C5, 0x00000006, 0x0000076D, 0x00000768,
    0x0000076C, 0x000200F9, 0x00000771, 0x000200F8, 0x00000762, 0x00050051,
    0x00000006, 0x00000764, 0x000006F8, 0x00000000, 0x000200F9, 0x00000771,
    0x000200F8, 0x00000771, 0x000900F5, 0x00000006, 0x00001051, 0x00000764,
    0x00000762, 0x0000076D, 0x00000765, 0x00000770, 0x0000076E, 0x000300F7,
    0x00000791, 0x00000000, 0x000400FA, 0x000006AF, 0x00000789, 0x0000078B,
    0x000200F8, 0x0000078B, 0x000500AA, 0x00000030, 0x0000078D, 0x000005E2,
    0x00000194, 0x000600A9, 0x00000006, 0x00001113, 0x0000078D, 0x0000009F,
    0x000000E8, 0x000200F9, 0x00000791, 0x000200F8, 0x00000789, 0x000200F9,
    0x00000791, 0x000200F8, 0x00000791, 0x000700F5, 0x00000006, 0x0000105D,
    0x000005E2, 0x00000789, 0x00001113, 0x0000078B, 0x00050080, 0x00000006,
    0x0000079A, 0x000006BF, 0x0000009C, 0x00050050, 0x00000008, 0x000007A0,
    0x0000079A, 0x000006C6, 0x00050080, 0x00000008, 0x000007A3, 0x000007A0,
    0x000005A4, 0x000500C4, 0x00000008, 0x000007A5, 0x000007A3, 0x000003A4,
    0x00050050, 0x00000008, 0x000007B5, 0x0000105D, 0x0000105D, 0x000500C2,
    0x00000008, 0x000007AE, 0x000007B5, 0x000002DD, 0x000500C7, 0x00000008,
    0x000007B0, 0x000007AE, 0x00001100, 0x00050080, 0x00000008, 0x000007A8,
    0x000007A5, 0x000007B0, 0x00050051, 0x00000006, 0x000007E7, 0x000007A8,
    0x00000000, 0x00050086, 0x00000006, 0x000007E9, 0x000007E7, 0x0000025C,
    0x00050051, 0x00000006, 0x000007EB, 0x000007A8, 0x00000001, 0x00050086,
    0x00000006, 0x000007ED, 0x000007EB, 0x000000BB, 0x00050084, 0x00000006,
    0x000007F2, 0x000007E9, 0x0000025C, 0x00050082, 0x00000006, 0x000007F3,
    0x000007E7, 0x000007F2, 0x00050084, 0x00000006, 0x000007F8, 0x000007ED,
    0x000000BB, 0x00050082, 0x00000006, 0x000007F9, 0x000007EB, 0x000007F8,
    0x00050084, 0x00000006, 0x000007FD, 0x000007ED, 0x00000723, 0x00050080,
    0x00000006, 0x000007FF, 0x000007FD, 0x000007E9, 0x00050080, 0x00000006,
    0x00000803, 0x00000728, 0x000007FF, 0x00050082, 0x00000006, 0x00000807,
    0x00000803, 0x0000072D, 0x00050086, 0x00000006, 0x0000080C, 0x00000807,
    0x00000730, 0x00050084, 0x00000006, 0x00000810, 0x0000080C, 0x00000730,
    0x00050082, 0x00000006, 0x00000811, 0x00000807, 0x00000810, 0x00050084,
    0x00000006, 0x00000814, 0x00000811, 0x0000025C, 0x00050080, 0x00000006,
    0x00000816, 0x00000814, 0x000007F3, 0x00050084, 0x00000006, 0x00000819,
    0x0000080C, 0x000000BB, 0x00050080, 0x00000006, 0x0000081B, 0x00000819,
    0x000007F9, 0x000500C7, 0x00000006, 0x0000082B, 0x0000081B, 0x0000009C,
    0x000500AB, 0x00000030, 0x0000082C, 0x0000082B, 0x000000E8, 0x000300F7,
    0x00000833, 0x00000000, 0x000400FA, 0x0000082C, 0x0000082D, 0x00000830,
    0x000200F8, 0x00000830, 0x00050041, 0x000002B0, 0x00000831, 0x000002AF,
    0x000000D9, 0x0004003D, 0x00000006, 0x00000832, 0x00000831, 0x000200F9,
    0x00000833, 0x000200F8, 0x0000082D, 0x00050041, 0x000002B0, 0x0000082E,
    0x000002AF, 0x00000192, 0x0004003D, 0x00000006, 0x0000082F, 0x0000082E,
    0x000200F9, 0x00000833, 0x000200F8, 0x00000833, 0x000700F5, 0x00000006,
    0x0000105F, 0x0000082F, 0x0000082D, 0x00000832, 0x00000830, 0x0004007C,
    0x00000014, 0x000007C6, 0x00000816, 0x000500C2, 0x00000006, 0x000007C9,
    0x0000081B, 0x0000009C, 0x0004007C, 0x00000014, 0x000007CA, 0x000007C9,
    0x00050050, 0x0000001C, 0x000007CE, 0x000007C6, 0x000007CA, 0x0004007C,
    0x00000014, 0x000007D0, 0x0000105F, 0x0007005F, 0x0000000D, 0x000007D1,
    0x000006EA, 0x000007CE, 0x00000040, 0x000007D0, 0x000300F7, 0x0000084A,
    0x00000000, 0x000900FB, 0x0000058E, 0x0000083B, 0x00000004, 0x0000083E,
    0x00000006, 0x0000083E, 0x0000000E, 0x00000847, 0x000200F8, 0x00000847,
    0x00050051, 0x00000006, 0x00000849, 0x000007D1, 0x00000000, 0x000200F9,
    0x0000084A, 0x000200F8, 0x0000083E, 0x00050051, 0x00000006, 0x00000840,
    0x000007D1, 0x00000000, 0x000500C7, 0x00000006, 0x00000841, 0x00000840,
    0x0000027B, 0x00050051, 0x00000006, 0x00000843, 0x000007D1, 0x00000001,
    0x000500C7, 0x00000006, 0x00000844, 0x00000843, 0x0000027B, 0x000500C4,
    0x00000006, 0x00000845, 0x00000844, 0x000000BB, 0x000500C5, 0x00000006,
    0x00000846, 0x00000841, 0x00000845, 0x000200F9, 0x0000084A, 0x000200F8,
    0x0000083B, 0x00050051, 0x00000006, 0x0000083D, 0x000007D1, 0x00000000,
    0x000200F9, 0x0000084A, 0x000200F8, 0x0000084A, 0x000900F5, 0x00000006,
    0x00001062, 0x0000083D, 0x0000083B, 0x00000846, 0x0000083E, 0x00000849,
    0x00000847, 0x000300F7, 0x0000086A, 0x00000000, 0x000400FA, 0x000006AF,
    0x00000862, 0x00000864, 0x000200F8, 0x00000864, 0x000500AA, 0x00000030,
    0x00000866, 0x000005E2, 0x00000194, 0x000600A9, 0x00000006, 0x00001114,
    0x00000866, 0x0000009F, 0x000000E8, 0x000200F9, 0x0000086A, 0x000200F8,
    0x00000862, 0x000200F9, 0x0000086A, 0x000200F8, 0x0000086A, 0x000700F5,
    0x00000006, 0x00001069, 0x000005E2, 0x00000862, 0x00001114, 0x00000864,
    0x00050080, 0x00000006, 0x00000873, 0x000006BF, 0x0000009F, 0x00050050,
    0x00000008, 0x00000879, 0x00000873, 0x000006C6, 0x00050080, 0x00000008,
    0x0000087C, 0x00000879, 0x000005A4, 0x000500C4, 0x00000008, 0x0000087E,
    0x0000087C, 0x000003A4, 0x00050050, 0x00000008, 0x0000088E, 0x00001069,
    0x00001069, 0x000500C2, 0x00000008, 0x00000887, 0x0000088E, 0x000002DD,
    0x000500C7, 0x00000008, 0x00000889, 0x00000887, 0x00001100, 0x00050080,
    0x00000008, 0x00000881, 0x0000087E, 0x00000889, 0x00050051, 0x00000006,
    0x000008C0, 0x00000881, 0x00000000, 0x00050086, 0x00000006, 0x000008C2,
    0x000008C0, 0x0000025C, 0x00050051, 0x00000006, 0x000008C4, 0x00000881,
    0x00000001, 0x00050086, 0x00000006, 0x000008C6, 0x000008C4, 0x000000BB,
    0x00050084, 0x00000006, 0x000008CB, 0x000008C2, 0x0000025C, 0x00050082,
    0x00000006, 0x000008CC, 0x000008C0, 0x000008CB, 0x00050084, 0x00000006,
    0x000008D1, 0x000008C6, 0x000000BB, 0x00050082, 0x00000006, 0x000008D2,
    0x000008C4, 0x000008D1, 0x00050084, 0x00000006, 0x000008D6, 0x000008C6,
    0x00000723, 0x00050080, 0x00000006, 0x000008D8, 0x000008D6, 0x000008C2,
    0x00050080, 0x00000006, 0x000008DC, 0x00000728, 0x000008D8, 0x00050082,
    0x00000006, 0x000008E0, 0x000008DC, 0x0000072D, 0x00050086, 0x00000006,
    0x000008E5, 0x000008E0, 0x00000730, 0x00050084, 0x00000006, 0x000008E9,
    0x000008E5, 0x00000730, 0x00050082, 0x00000006, 0x000008EA, 0x000008E0,
    0x000008E9, 0x00050084, 0x00000006, 0x000008ED, 0x000008EA, 0x0000025C,
    0x00050080, 0x00000006, 0x000008EF, 0x000008ED, 0x000008CC, 0x00050084,
    0x00000006, 0x000008F2, 0x000008E5, 0x000000BB, 0x00050080, 0x00000006,
    0x000008F4, 0x000008F2, 0x000008D2, 0x000500C7, 0x00000006, 0x00000904,
    0x000008F4, 0x0000009C, 0x000500AB, 0x00000030, 0x00000905, 0x00000904,
    0x000000E8, 0x000300F7, 0x0000090C, 0x00000000, 0x000400FA, 0x00000905,
    0x00000906, 0x00000909, 0x000200F8, 0x00000909, 0x00050041, 0x000002B0,
    0x0000090A, 0x000002AF, 0x000000D9, 0x0004003D, 0x00000006, 0x0000090B,
    0x0000090A, 0x000200F9, 0x0000090C, 0x000200F8, 0x00000906, 0x00050041,
    0x000002B0, 0x00000907, 0x000002AF, 0x00000192, 0x0004003D, 0x00000006,
    0x00000908, 0x00000907, 0x000200F9, 0x0000090C, 0x000200F8, 0x0000090C,
    0x000700F5, 0x00000006, 0x0000106B, 0x00000908, 0x00000906, 0x0000090B,
    0x00000909, 0x0004007C, 0x00000014, 0x0000089F, 0x000008EF, 0x000500C2,
    0x00000006, 0x000008A2, 0x000008F4, 0x0000009C, 0x0004007C, 0x00000014,
    0x000008A3, 0x000008A2, 0x00050050, 0x0000001C, 0x000008A7, 0x0000089F,
    0x000008A3, 0x0004007C, 0x00000014, 0x000008A9, 0x0000106B, 0x0007005F,
    0x0000000D, 0x000008AA, 0x000006EA, 0x000008A7, 0x00000040, 0x000008A9,
    0x000300F7, 0x00000923, 0x00000000, 0x000900FB, 0x0000058E, 0x00000914,
    0x00000004, 0x00000917, 0x00000006, 0x00000917, 0x0000000E, 0x00000920,
    0x000200F8, 0x00000920, 0x00050051, 0x00000006, 0x00000922, 0x000008AA,
    0x00000000, 0x000200F9, 0x00000923, 0x000200F8, 0x00000917, 0x00050051,
    0x00000006, 0x00000919, 0x000008AA, 0x00000000, 0x000500C7, 0x00000006,
    0x0000091A, 0x00000919, 0x0000027B, 0x00050051, 0x00000006, 0x0000091C,
    0x000008AA, 0x00000001, 0x000500C7, 0x00000006, 0x0000091D, 0x0000091C,
    0x0000027B, 0x000500C4, 0x00000006, 0x0000091E, 0x0000091D, 0x000000BB,
    0x000500C5, 0x00000006, 0x0000091F, 0x0000091A, 0x0000091E, 0x000200F9,
    0x00000923, 0x000200F8, 0x00000914, 0x00050051, 0x00000006, 0x00000916,
    0x000008AA, 0x00000000, 0x000200F9, 0x00000923, 0x000200F8, 0x00000923,
    0x000900F5, 0x00000006, 0x0000106E, 0x00000916, 0x00000914, 0x0000091F,
    0x00000917, 0x00000922, 0x00000920, 0x000300F7, 0x00000943, 0x00000000,
    0x000400FA, 0x000006AF, 0x0000093B, 0x0000093D, 0x000200F8, 0x0000093D,
    0x000500AA, 0x00000030, 0x0000093F, 0x000005E2, 0x00000194, 0x000600A9,
    0x00000006, 0x00001115, 0x0000093F, 0x0000009F, 0x000000E8, 0x000200F9,
    0x00000943, 0x000200F8, 0x0000093B, 0x000200F9, 0x00000943, 0x000200F8,
    0x00000943, 0x000700F5, 0x00000006, 0x00001075, 0x000005E2, 0x0000093B,
    0x00001115, 0x0000093D, 0x00050080, 0x00000006, 0x0000094C, 0x000006BF,
    0x000000B5, 0x00050050, 0x00000008, 0x00000952, 0x0000094C, 0x000006C6,
    0x00050080, 0x00000008, 0x00000955, 0x00000952, 0x000005A4, 0x000500C4,
    0x00000008, 0x00000957, 0x00000955, 0x000003A4, 0x00050050, 0x00000008,
    0x00000967, 0x00001075, 0x00001075, 0x000500C2, 0x00000008, 0x00000960,
    0x00000967, 0x000002DD, 0x000500C7, 0x00000008, 0x00000962, 0x00000960,
    0x00001100, 0x00050080, 0x00000008, 0x0000095A, 0x00000957, 0x00000962,
    0x00050051, 0x00000006, 0x00000999, 0x0000095A, 0x00000000, 0x00050086,
    0x00000006, 0x0000099B, 0x00000999, 0x0000025C, 0x00050051, 0x00000006,
    0x0000099D, 0x0000095A, 0x00000001, 0x00050086, 0x00000006, 0x0000099F,
    0x0000099D, 0x000000BB, 0x00050084, 0x00000006, 0x000009A4, 0x0000099B,
    0x0000025C, 0x00050082, 0x00000006, 0x000009A5, 0x00000999, 0x000009A4,
    0x00050084, 0x00000006, 0x000009AA, 0x0000099F, 0x000000BB, 0x00050082,
    0x00000006, 0x000009AB, 0x0000099D, 0x000009AA, 0x00050084, 0x00000006,
    0x000009AF, 0x0000099F, 0x00000723, 0x00050080, 0x00000006, 0x000009B1,
    0x000009AF, 0x0000099B, 0x00050080, 0x00000006, 0x000009B5, 0x00000728,
    0x000009B1, 0x00050082, 0x00000006, 0x000009B9, 0x000009B5, 0x0000072D,
    0x00050086, 0x00000006, 0x000009BE, 0x000009B9, 0x00000730, 0x00050084,
    0x00000006, 0x000009C2, 0x000009BE, 0x00000730, 0x00050082, 0x00000006,
    0x000009C3, 0x000009B9, 0x000009C2, 0x00050084, 0x00000006, 0x000009C6,
    0x000009C3, 0x0000025C, 0x00050080, 0x00000006, 0x000009C8, 0x000009C6,
    0x000009A5, 0x00050084, 0x00000006, 0x000009CB, 0x000009BE, 0x000000BB,
    0x00050080, 0x00000006, 0x000009CD, 0x000009CB, 0x000009AB, 0x000500C7,
    0x00000006, 0x000009DD, 0x000009CD, 0x0000009C, 0x000500AB, 0x00000030,
    0x000009DE, 0x000009DD, 0x000000E8, 0x000300F7, 0x000009E5, 0x00000000,
    0x000400FA, 0x000009DE, 0x000009DF, 0x000009E2, 0x000200F8, 0x000009E2,
    0x00050041, 0x000002B0, 0x000009E3, 0x000002AF, 0x000000D9, 0x0004003D,
    0x00000006, 0x000009E4, 0x000009E3, 0x000200F9, 0x000009E5, 0x000200F8,
    0x000009DF, 0x00050041, 0x000002B0, 0x000009E0, 0x000002AF, 0x00000192,
    0x0004003D, 0x00000006, 0x000009E1, 0x000009E0, 0x000200F9, 0x000009E5,
    0x000200F8, 0x000009E5, 0x000700F5, 0x00000006, 0x00001077, 0x000009E1,
    0x000009DF, 0x000009E4, 0x000009E2, 0x0004007C, 0x00000014, 0x00000978,
    0x000009C8, 0x000500C2, 0x00000006, 0x0000097B, 0x000009CD, 0x0000009C,
    0x0004007C, 0x00000014, 0x0000097C, 0x0000097B, 0x00050050, 0x0000001C,
    0x00000980, 0x00000978, 0x0000097C, 0x0004007C, 0x00000014, 0x00000982,
    0x00001077, 0x0007005F, 0x0000000D, 0x00000983, 0x000006EA, 0x00000980,
    0x00000040, 0x00000982, 0x000300F7, 0x000009FC, 0x00000000, 0x000900FB,
    0x0000058E, 0x000009ED, 0x00000004, 0x000009F0, 0x00000006, 0x000009F0,
    0x0000000E, 0x000009F9, 0x000200F8, 0x000009F9, 0x00050051, 0x00000006,
    0x000009FB, 0x00000983, 0x00000000, 0x000200F9, 0x000009FC, 0x000200F8,
    0x000009F0, 0x00050051, 0x00000006, 0x000009F2, 0x00000983, 0x00000000,
    0x000500C7, 0x00000006, 0x000009F3, 0x000009F2, 0x0000027B, 0x00050051,
    0x00000006, 0x000009F5, 0x00000983, 0x00000001, 0x000500C7, 0x00000006,
    0x000009F6, 0x000009F5, 0x0000027B, 0x000500C4, 0x00000006, 0x000009F7,
    0x000009F6, 0x000000BB, 0x000500C5, 0x00000006, 0x000009F8, 0x000009F3,
    0x000009F7, 0x000200F9, 0x000009FC, 0x000200F8, 0x000009ED, 0x00050051,
    0x00000006, 0x000009EF, 0x00000983, 0x00000000, 0x000200F9, 0x000009FC,
    0x000200F8, 0x000009FC, 0x000900F5, 0x00000006, 0x0000107A, 0x000009EF,
    0x000009ED, 0x000009F8, 0x000009F0, 0x000009FB, 0x000009F9, 0x00070050,
    0x0000000D, 0x0000110D, 0x00001051, 0x00001062, 0x0000106E, 0x0000107A,
    0x000300F7, 0x00000A1C, 0x00000000, 0x000400FA, 0x000006AF, 0x00000A14,
    0x00000A16, 0x000200F8, 0x00000A16, 0x000500AA, 0x00000030, 0x00000A18,
    0x000005E2, 0x00000194, 0x000600A9, 0x00000006, 0x00001116, 0x00000A18,
    0x0000009F, 0x000000E8, 0x000200F9, 0x00000A1C, 0x000200F8, 0x00000A14,
    0x000200F9, 0x00000A1C, 0x000200F8, 0x00000A1C, 0x000700F5, 0x00000006,
    0x00001081, 0x000005E2, 0x00000A14, 0x00001116, 0x00000A16, 0x00050080,
    0x00000006, 0x00000A25, 0x000006BF, 0x00000182, 0x00050050, 0x00000008,
    0x00000A2B, 0x00000A25, 0x000006C6, 0x00050080, 0x00000008, 0x00000A2E,
    0x00000A2B, 0x000005A4, 0x000500C4, 0x00000008, 0x00000A30, 0x00000A2E,
    0x000003A4, 0x00050050, 0x00000008, 0x00000A40, 0x00001081, 0x00001081,
    0x000500C2, 0x00000008, 0x00000A39, 0x00000A40, 0x000002DD, 0x000500C7,
    0x00000008, 0x00000A3B, 0x00000A39, 0x00001100, 0x00050080, 0x00000008,
    0x00000A33, 0x00000A30, 0x00000A3B, 0x00050051, 0x00000006, 0x00000A72,
    0x00000A33, 0x00000000, 0x00050086, 0x00000006, 0x00000A74, 0x00000A72,
    0x0000025C, 0x00050051, 0x00000006, 0x00000A76, 0x00000A33, 0x00000001,
    0x00050086, 0x00000006, 0x00000A78, 0x00000A76, 0x000000BB, 0x00050084,
    0x00000006, 0x00000A7D, 0x00000A74, 0x0000025C, 0x00050082, 0x00000006,
    0x00000A7E, 0x00000A72, 0x00000A7D, 0x00050084, 0x00000006, 0x00000A83,
    0x00000A78, 0x000000BB, 0x00050082, 0x00000006, 0x00000A84, 0x00000A76,
    0x00000A83, 0x00050084, 0x00000006, 0x00000A88, 0x00000A78, 0x00000723,
    0x00050080, 0x00000006, 0x00000A8A, 0x00000A88, 0x00000A74, 0x00050080,
    0x00000006, 0x00000A8E, 0x00000728, 0x00000A8A, 0x00050082, 0x00000006,
    0x00000A92, 0x00000A8E, 0x0000072D, 0x00050086, 0x00000006, 0x00000A97,
    0x00000A92, 0x00000730, 0x00050084, 0x00000006, 0x00000A9B, 0x00000A97,
    0x00000730, 0x00050082, 0x00000006, 0x00000A9C, 0x00000A92, 0x00000A9B,
    0x00050084, 0x00000006, 0x00000A9F, 0x00000A9C, 0x0000025C, 0x00050080,
    0x00000006, 0x00000AA1, 0x00000A9F, 0x00000A7E, 0x00050084, 0x00000006,
    0x00000AA4, 0x00000A97, 0x000000BB, 0x00050080, 0x00000006, 0x00000AA6,
    0x00000AA4, 0x00000A84, 0x000500C7, 0x00000006, 0x00000AB6, 0x00000AA6,
    0x0000009C, 0x000500AB, 0x00000030, 0x00000AB7, 0x00000AB6, 0x000000E8,
    0x000300F7, 0x00000ABE, 0x00000000, 0x000400FA, 0x00000AB7, 0x00000AB8,
    0x00000ABB, 0x000200F8, 0x00000ABB, 0x00050041, 0x000002B0, 0x00000ABC,
    0x000002AF, 0x000000D9, 0x0004003D, 0x00000006, 0x00000ABD, 0x00000ABC,
    0x000200F9, 0x00000ABE, 0x000200F8, 0x00000AB8, 0x00050041, 0x000002B0,
    0x00000AB9, 0x000002AF, 0x00000192, 0x0004003D, 0x00000006, 0x00000ABA,
    0x00000AB9, 0x000200F9, 0x00000ABE, 0x000200F8, 0x00000ABE, 0x000700F5,
    0x00000006, 0x00001083, 0x00000ABA, 0x00000AB8, 0x00000ABD, 0x00000ABB,
    0x0004007C, 0x00000014, 0x00000A51, 0x00000AA1, 0x000500C2, 0x00000006,
    0x00000A54, 0x00000AA6, 0x0000009C, 0x0004007C, 0x00000014, 0x00000A55,
    0x00000A54, 0x00050050, 0x0000001C, 0x00000A59, 0x00000A51, 0x00000A55,
    0x0004007C, 0x00000014, 0x00000A5B, 0x00001083, 0x0007005F, 0x0000000D,
    0x00000A5C, 0x000006EA, 0x00000A59, 0x00000040, 0x00000A5B, 0x000300F7,
    0x00000AD5, 0x00000000, 0x000900FB, 0x0000058E, 0x00000AC6, 0x00000004,
    0x00000AC9, 0x00000006, 0x00000AC9, 0x0000000E, 0x00000AD2, 0x000200F8,
    0x00000AD2, 0x00050051, 0x00000006, 0x00000AD4, 0x00000A5C, 0x00000000,
    0x000200F9, 0x00000AD5, 0x000200F8, 0x00000AC9, 0x00050051, 0x00000006,
    0x00000ACB, 0x00000A5C, 0x00000000, 0x000500C7, 0x00000006, 0x00000ACC,
    0x00000ACB, 0x0000027B, 0x00050051, 0x00000006, 0x00000ACE, 0x00000A5C,
    0x00000001, 0x000500C7, 0x00000006, 0x00000ACF, 0x00000ACE, 0x0000027B,
    0x000500C4, 0x00000006, 0x00000AD0, 0x00000ACF, 0x000000BB, 0x000500C5,
    0x00000006, 0x00000AD1, 0x00000ACC, 0x00000AD0, 0x000200F9, 0x00000AD5,
    0x000200F8, 0x00000AC6, 0x00050051, 0x00000006, 0x00000AC8, 0x00000A5C,
    0x00000000, 0x000200F9, 0x00000AD5, 0x000200F8, 0x00000AD5, 0x000900F5,
    0x00000006, 0x00001086, 0x00000AC8, 0x00000AC6, 0x00000AD1, 0x00000AC9,
    0x00000AD4, 0x00000AD2, 0x000300F7, 0x00000AF5, 0x00000000, 0x000400FA,
    0x000006AF, 0x00000AED, 0x00000AEF, 0x000200F8, 0x00000AEF, 0x000500AA,
    0x00000030, 0x00000AF1, 0x000005E2, 0x00000194, 0x000600A9, 0x00000006,
    0x00001117, 0x00000AF1, 0x0000009F, 0x000000E8, 0x000200F9, 0x00000AF5,
    0x000200F8, 0x00000AED, 0x000200F9, 0x00000AF5, 0x000200F8, 0x00000AF5,
    0x000700F5, 0x00000006, 0x000010A9, 0x000005E2, 0x00000AED, 0x00001117,
    0x00000AEF, 0x00050080, 0x00000006, 0x00000AFE, 0x000006BF, 0x00000194,
    0x00050050, 0x00000008, 0x00000B04, 0x00000AFE, 0x000006C6, 0x00050080,
    0x00000008, 0x00000B07, 0x00000B04, 0x000005A4, 0x000500C4, 0x00000008,
    0x00000B09, 0x00000B07, 0x000003A4, 0x00050050, 0x00000008, 0x00000B19,
    0x000010A9, 0x000010A9, 0x000500C2, 0x00000008, 0x00000B12, 0x00000B19,
    0x000002DD, 0x000500C7, 0x00000008, 0x00000B14, 0x00000B12, 0x00001100,
    0x00050080, 0x00000008, 0x00000B0C, 0x00000B09, 0x00000B14, 0x00050051,
    0x00000006, 0x00000B4B, 0x00000B0C, 0x00000000, 0x00050086, 0x00000006,
    0x00000B4D, 0x00000B4B, 0x0000025C, 0x00050051, 0x00000006, 0x00000B4F,
    0x00000B0C, 0x00000001, 0x00050086, 0x00000006, 0x00000B51, 0x00000B4F,
    0x000000BB, 0x00050084, 0x00000006, 0x00000B56, 0x00000B4D, 0x0000025C,
    0x00050082, 0x00000006, 0x00000B57, 0x00000B4B, 0x00000B56, 0x00050084,
    0x00000006, 0x00000B5C, 0x00000B51, 0x000000BB, 0x00050082, 0x00000006,
    0x00000B5D, 0x00000B4F, 0x00000B5C, 0x00050084, 0x00000006, 0x00000B61,
    0x00000B51, 0x00000723, 0x00050080, 0x00000006, 0x00000B63, 0x00000B61,
    0x00000B4D, 0x00050080, 0x00000006, 0x00000B67, 0x00000728, 0x00000B63,
    0x00050082, 0x00000006, 0x00000B6B, 0x00000B67, 0x0000072D, 0x00050086,
    0x00000006, 0x00000B70, 0x00000B6B, 0x00000730, 0x00050084, 0x00000006,
    0x00000B74, 0x00000B70, 0x00000730, 0x00050082, 0x00000006, 0x00000B75,
    0x00000B6B, 0x00000B74, 0x00050084, 0x00000006, 0x00000B78, 0x00000B75,
    0x0000025C, 0x00050080, 0x00000006, 0x00000B7A, 0x00000B78, 0x00000B57,
    0x00050084, 0x00000006, 0x00000B7D, 0x00000B70, 0x000000BB, 0x00050080,
    0x00000006, 0x00000B7F, 0x00000B7D, 0x00000B5D, 0x000500C7, 0x00000006,
    0x00000B8F, 0x00000B7F, 0x0000009C, 0x000500AB, 0x00000030, 0x00000B90,
    0x00000B8F, 0x000000E8, 0x000300F7, 0x00000B97, 0x00000000, 0x000400FA,
    0x00000B90, 0x00000B91, 0x00000B94, 0x000200F8, 0x00000B94, 0x00050041,
    0x000002B0, 0x00000B95, 0x000002AF, 0x000000D9, 0x0004003D, 0x00000006,
    0x00000B96, 0x00000B95, 0x000200F9, 0x00000B97, 0x000200F8, 0x00000B91,
    0x00050041, 0x000002B0, 0x00000B92, 0x000002AF, 0x00000192, 0x0004003D,
    0x00000006, 0x00000B93, 0x00000B92, 0x000200F9, 0x00000B97, 0x000200F8,
    0x00000B97, 0x000700F5, 0x00000006, 0x000010AB, 0x00000B93, 0x00000B91,
    0x00000B96, 0x00000B94, 0x0004007C, 0x00000014, 0x00000B2A, 0x00000B7A,
    0x000500C2, 0x00000006, 0x00000B2D, 0x00000B7F, 0x0000009C, 0x0004007C,
    0x00000014, 0x00000B2E, 0x00000B2D, 0x00050050, 0x0000001C, 0x00000B32,
    0x00000B2A, 0x00000B2E, 0x0004007C, 0x00000014, 0x00000B34, 0x000010AB,
    0x0007005F, 0x0000000D, 0x00000B35, 0x000006EA, 0x00000B32, 0x00000040,
    0x00000B34, 0x000300F7, 0x00000BAE, 0x00000000, 0x000900FB, 0x0000058E,
    0x00000B9F, 0x00000004, 0x00000BA2, 0x00000006, 0x00000BA2, 0x0000000E,
    0x00000BAB, 0x000200F8, 0x00000BAB, 0x00050051, 0x00000006, 0x00000BAD,
    0x00000B35, 0x00000000, 0x000200F9, 0x00000BAE, 0x000200F8, 0x00000BA2,
    0x00050051, 0x00000006, 0x00000BA4, 0x00000B35, 0x00000000, 0x000500C7,
    0x00000006, 0x00000BA5, 0x00000BA4, 0x0000027B, 0x00050051, 0x00000006,
    0x00000BA7, 0x00000B35, 0x00000001, 0x000500C7, 0x00000006, 0x00000BA8,
    0x00000BA7, 0x0000027B, 0x000500C4, 0x00000006, 0x00000BA9, 0x00000BA8,
    0x000000BB, 0x000500C5, 0x00000006, 0x00000BAA, 0x00000BA5, 0x00000BA9,
    0x000200F9, 0x00000BAE, 0x000200F8, 0x00000B9F, 0x00050051, 0x00000006,
    0x00000BA1, 0x00000B35, 0x00000000, 0x000200F9, 0x00000BAE, 0x000200F8,
    0x00000BAE, 0x000900F5, 0x00000006, 0x000010AE, 0x00000BA1, 0x00000B9F,
    0x00000BAA, 0x00000BA2, 0x00000BAD, 0x00000BAB, 0x000300F7, 0x00000BCE,
    0x00000000, 0x000400FA, 0x000006AF, 0x00000BC6, 0x00000BC8, 0x000200F8,
    0x00000BC8, 0x000500AA, 0x00000030, 0x00000BCA, 0x000005E2, 0x00000194,
    0x000600A9, 0x00000006, 0x00001118, 0x00000BCA, 0x0000009F, 0x000000E8,
    0x000200F9, 0x00000BCE, 0x000200F8, 0x00000BC6, 0x000200F9, 0x00000BCE,
    0x000200F8, 0x00000BCE, 0x000700F5, 0x00000006, 0x000010B5, 0x000005E2,
    0x00000BC6, 0x00001118, 0x00000BC8, 0x00050080, 0x00000006, 0x00000BD7,
    0x000006BF, 0x00000448, 0x00050050, 0x00000008, 0x00000BDD, 0x00000BD7,
    0x000006C6, 0x00050080, 0x00000008, 0x00000BE0, 0x00000BDD, 0x000005A4,
    0x000500C4, 0x00000008, 0x00000BE2, 0x00000BE0, 0x000003A4, 0x00050050,
    0x00000008, 0x00000BF2, 0x000010B5, 0x000010B5, 0x000500C2, 0x00000008,
    0x00000BEB, 0x00000BF2, 0x000002DD, 0x000500C7, 0x00000008, 0x00000BED,
    0x00000BEB, 0x00001100, 0x00050080, 0x00000008, 0x00000BE5, 0x00000BE2,
    0x00000BED, 0x00050051, 0x00000006, 0x00000C24, 0x00000BE5, 0x00000000,
    0x00050086, 0x00000006, 0x00000C26, 0x00000C24, 0x0000025C, 0x00050051,
    0x00000006, 0x00000C28, 0x00000BE5, 0x00000001, 0x00050086, 0x00000006,
    0x00000C2A, 0x00000C28, 0x000000BB, 0x00050084, 0x00000006, 0x00000C2F,
    0x00000C26, 0x0000025C, 0x00050082, 0x00000006, 0x00000C30, 0x00000C24,
    0x00000C2F, 0x00050084, 0x00000006, 0x00000C35, 0x00000C2A, 0x000000BB,
    0x00050082, 0x00000006, 0x00000C36, 0x00000C28, 0x00000C35, 0x00050084,
    0x00000006, 0x00000C3A, 0x00000C2A, 0x00000723, 0x00050080, 0x00000006,
    0x00000C3C, 0x00000C3A, 0x00000C26, 0x00050080, 0x00000006, 0x00000C40,
    0x00000728, 0x00000C3C, 0x00050082, 0x00000006, 0x00000C44, 0x00000C40,
    0x0000072D, 0x00050086, 0x00000006, 0x00000C49, 0x00000C44, 0x00000730,
    0x00050084, 0x00000006, 0x00000C4D, 0x00000C49, 0x00000730, 0x00050082,
    0x00000006, 0x00000C4E, 0x00000C44, 0x00000C4D, 0x00050084, 0x00000006,
    0x00000C51, 0x00000C4E, 0x0000025C, 0x00050080, 0x00000006, 0x00000C53,
    0x00000C51, 0x00000C30, 0x00050084, 0x00000006, 0x00000C56, 0x00000C49,
    0x000000BB, 0x00050080, 0x00000006, 0x00000C58, 0x00000C56, 0x00000C36,
    0x000500C7, 0x00000006, 0x00000C68, 0x00000C58, 0x0000009C, 0x000500AB,
    0x00000030, 0x00000C69, 0x00000C68, 0x000000E8, 0x000300F7, 0x00000C70,
    0x00000000, 0x000400FA, 0x00000C69, 0x00000C6A, 0x00000C6D, 0x000200F8,
    0x00000C6D, 0x00050041, 0x000002B0, 0x00000C6E, 0x000002AF, 0x000000D9,
    0x0004003D, 0x00000006, 0x00000C6F, 0x00000C6E, 0x000200F9, 0x00000C70,
    0x000200F8, 0x00000C6A, 0x00050041, 0x000002B0, 0x00000C6B, 0x000002AF,
    0x00000192, 0x0004003D, 0x00000006, 0x00000C6C, 0x00000C6B, 0x000200F9,
    0x00000C70, 0x000200F8, 0x00000C70, 0x000700F5, 0x00000006, 0x000010B7,
    0x00000C6C, 0x00000C6A, 0x00000C6F, 0x00000C6D, 0x0004007C, 0x00000014,
    0x00000C03, 0x00000C53, 0x000500C2, 0x00000006, 0x00000C06, 0x00000C58,
    0x0000009C, 0x0004007C, 0x00000014, 0x00000C07, 0x00000C06, 0x00050050,
    0x0000001C, 0x00000C0B, 0x00000C03, 0x00000C07, 0x0004007C, 0x00000014,
    0x00000C0D, 0x000010B7, 0x0007005F, 0x0000000D, 0x00000C0E, 0x000006EA,
    0x00000C0B, 0x00000040, 0x00000C0D, 0x000300F7, 0x00000C87, 0x00000000,
    0x000900FB, 0x0000058E, 0x00000C78, 0x00000004, 0x00000C7B, 0x00000006,
    0x00000C7B, 0x0000000E, 0x00000C84, 0x000200F8, 0x00000C84, 0x00050051,
    0x00000006, 0x00000C86, 0x00000C0E, 0x00000000, 0x000200F9, 0x00000C87,
    0x000200F8, 0x00000C7B, 0x00050051, 0x00000006, 0x00000C7D, 0x00000C0E,
    0x00000000, 0x000500C7, 0x00000006, 0x00000C7E, 0x00000C7D, 0x0000027B,
    0x00050051, 0x00000006, 0x00000C80, 0x00000C0E, 0x00000001, 0x000500C7,
    0x00000006, 0x00000C81, 0x00000C80, 0x0000027B, 0x000500C4, 0x00000006,
    0x00000C82, 0x00000C81, 0x000000BB, 0x000500C5, 0x00000006, 0x00000C83,
    0x00000C7E, 0x00000C82, 0x000200F9, 0x00000C87, 0x000200F8, 0x00000C78,
    0x00050051, 0x00000006, 0x00000C7A, 0x00000C0E, 0x00000000, 0x000200F9,
    0x00000C87, 0x000200F8, 0x00000C87, 0x000900F5, 0x00000006, 0x000010BA,
    0x00000C7A, 0x00000C78, 0x00000C83, 0x00000C7B, 0x00000C86, 0x00000C84,
    0x000300F7, 0x00000CA7, 0x00000000, 0x000400FA, 0x000006AF, 0x00000C9F,
    0x00000CA1, 0x000200F8, 0x00000CA1, 0x000500AA, 0x00000030, 0x00000CA3,
    0x000005E2, 0x00000194, 0x000600A9, 0x00000006, 0x00001119, 0x00000CA3,
    0x0000009F, 0x000000E8, 0x000200F9, 0x00000CA7, 0x000200F8, 0x00000C9F,
    0x000200F9, 0x00000CA7, 0x000200F8, 0x00000CA7, 0x000700F5, 0x00000006,
    0x000010C1, 0x000005E2, 0x00000C9F, 0x00001119, 0x00000CA1, 0x00050080,
    0x00000006, 0x00000CB0, 0x000006BF, 0x000001A3, 0x00050050, 0x00000008,
    0x00000CB6, 0x00000CB0, 0x000006C6, 0x00050080, 0x00000008, 0x00000CB9,
    0x00000CB6, 0x000005A4, 0x000500C4, 0x00000008, 0x00000CBB, 0x00000CB9,
    0x000003A4, 0x00050050, 0x00000008, 0x00000CCB, 0x000010C1, 0x000010C1,
    0x000500C2, 0x00000008, 0x00000CC4, 0x00000CCB, 0x000002DD, 0x000500C7,
    0x00000008, 0x00000CC6, 0x00000CC4, 0x00001100, 0x00050080, 0x00000008,
    0x00000CBE, 0x00000CBB, 0x00000CC6, 0x00050051, 0x00000006, 0x00000CFD,
    0x00000CBE, 0x00000000, 0x00050086, 0x00000006, 0x00000CFF, 0x00000CFD,
    0x0000025C, 0x00050051, 0x00000006, 0x00000D01, 0x00000CBE, 0x00000001,
    0x00050086, 0x00000006, 0x00000D03, 0x00000D01, 0x000000BB, 0x00050084,
    0x00000006, 0x00000D08, 0x00000CFF, 0x0000025C, 0x00050082, 0x00000006,
    0x00000D09, 0x00000CFD, 0x00000D08, 0x00050084, 0x00000006, 0x00000D0E,
    0x00000D03, 0x000000BB, 0x00050082, 0x00000006, 0x00000D0F, 0x00000D01,
    0x00000D0E, 0x00050084, 0x00000006, 0x00000D13, 0x00000D03, 0x00000723,
    0x00050080, 0x00000006, 0x00000D15, 0x00000D13, 0x00000CFF, 0x00050080,
    0x00000006, 0x00000D19, 0x00000728, 0x00000D15, 0x00050082, 0x00000006,
    0x00000D1D, 0x00000D19, 0x0000072D, 0x00050086, 0x00000006, 0x00000D22,
    0x00000D1D, 0x00000730, 0x00050084, 0x00000006, 0x00000D26, 0x00000D22,
    0x00000730, 0x00050082, 0x00000006, 0x00000D27, 0x00000D1D, 0x00000D26,
    0x00050084, 0x00000006, 0x00000D2A, 0x00000D27, 0x0000025C, 0x00050080,
    0x00000006, 0x00000D2C, 0x00000D2A, 0x00000D09, 0x00050084, 0x00000006,
    0x00000D2F, 0x00000D22, 0x000000BB, 0x00050080, 0x00000006, 0x00000D31,
    0x00000D2F, 0x00000D0F, 0x000500C7, 0x00000006, 0x00000D41, 0x00000D31,
    0x0000009C, 0x000500AB, 0x00000030, 0x00000D42, 0x00000D41, 0x000000E8,
    0x000300F7, 0x00000D49, 0x00000000, 0x000400FA, 0x00000D42, 0x00000D43,
    0x00000D46, 0x000200F8, 0x00000D46, 0x00050041, 0x000002B0, 0x00000D47,
    0x000002AF, 0x000000D9, 0x0004003D, 0x00000006, 0x00000D48, 0x00000D47,
    0x000200F9, 0x00000D49, 0x000200F8, 0x00000D43, 0x00050041, 0x000002B0,
    0x00000D44, 0x000002AF, 0x00000192, 0x0004003D, 0x00000006, 0x00000D45,
    0x00000D44, 0x000200F9, 0x00000D49, 0x000200F8, 0x00000D49, 0x000700F5,
    0x00000006, 0x000010C3, 0x00000D45, 0x00000D43, 0x00000D48, 0x00000D46,
    0x0004007C, 0x00000014, 0x00000CDC, 0x00000D2C, 0x000500C2, 0x00000006,
    0x00000CDF, 0x00000D31, 0x0000009C, 0x0004007C, 0x00000014, 0x00000CE0,
    0x00000CDF, 0x00050050, 0x0000001C, 0x00000CE4, 0x00000CDC, 0x00000CE0,
    0x0004007C, 0x00000014, 0x00000CE6, 0x000010C3, 0x0007005F, 0x0000000D,
    0x00000CE7, 0x000006EA, 0x00000CE4, 0x00000040, 0x00000CE6, 0x000300F7,
    0x00000D60, 0x00000000, 0x000900FB, 0x0000058E, 0x00000D51, 0x00000004,
    0x00000D54, 0x00000006, 0x00000D54, 0x0000000E, 0x00000D5D, 0x000200F8,
    0x00000D5D, 0x00050051, 0x00000006, 0x00000D5F, 0x00000CE7, 0x00000000,
    0x000200F9, 0x00000D60, 0x000200F8, 0x00000D54, 0x00050051, 0x00000006,
    0x00000D56, 0x00000CE7, 0x00000000, 0x000500C7, 0x00000006, 0x00000D57,
    0x00000D56, 0x0000027B, 0x00050051, 0x00000006, 0x00000D59, 0x00000CE7,
    0x00000001, 0x000500C7, 0x00000006, 0x00000D5A, 0x00000D59, 0x0000027B,
    0x000500C4, 0x00000006, 0x00000D5B, 0x00000D5A, 0x000000BB, 0x000500C5,
    0x00000006, 0x00000D5C, 0x00000D57, 0x00000D5B, 0x000200F9, 0x00000D60,
    0x000200F8, 0x00000D51, 0x00050051, 0x00000006, 0x00000D53, 0x00000CE7,
    0x00000000, 0x000200F9, 0x00000D60, 0x000200F8, 0x00000D60, 0x000900F5,
    0x00000006, 0x000010C6, 0x00000D53, 0x00000D51, 0x00000D5C, 0x00000D54,
    0x00000D5F, 0x00000D5D, 0x00070050, 0x0000000D, 0x0000110E, 0x00001086,
    0x000010AE, 0x000010BA, 0x000010C6, 0x000500AA, 0x00000030, 0x00000D68,
    0x000006BF, 0x000000E8, 0x000600A9, 0x00000030, 0x0000111A, 0x00000D68,
    0x00000320, 0x00000D68, 0x000300F7, 0x00000D83, 0x00000002, 0x000400FA,
    0x0000111A, 0x00000D6F, 0x00000D83, 0x000200F8, 0x00000D6F, 0x00060052,
    0x0000000D, 0x00001045, 0x00001062, 0x0000110D, 0x00000000, 0x000200F9,
    0x00000D83, 0x000200F8, 0x00000D83, 0x000700F5, 0x0000000D, 0x000010E8,
    0x0000110D, 0x00000D60, 0x00001045, 0x00000D6F, 0x000300F7, 0x00000D99,
    0x00000002, 0x000400FA, 0x000005CB, 0x00000D8B, 0x00000D99, 0x000200F8,
    0x00000D8B, 0x000300F7, 0x00000D98, 0x00000000, 0x000F00FB, 0x0000058E,
    0x00000D98, 0x00000000, 0x00000D8E, 0x00000001, 0x00000D8E, 0x00000002,
    0x00000D93, 0x00000003, 0x00000D93, 0x0000000A, 0x00000D93, 0x0000000C,
    0x00000D93, 0x000200F8, 0x00000D93, 0x000500C7, 0x0000000D, 0x00000DC0,
    0x000010E8, 0x00001107, 0x000500C7, 0x0000000D, 0x00000DC3, 0x000010E8,
    0x00001108, 0x000500C4, 0x0000000D, 0x00000DC5, 0x00000DC3, 0x00001109,
    0x000500C5, 0x0000000D, 0x00000DC6, 0x00000DC0, 0x00000DC5, 0x000500C2,
    0x0000000D, 0x00000DC9, 0x000010E8, 0x00001109, 0x000500C7, 0x0000000D,
    0x00000DCB, 0x00000DC9, 0x00001108, 0x000500C5, 0x0000000D, 0x00000DCC,
    0x00000DC6, 0x00000DCB, 0x000500C7, 0x0000000D, 0x00000DD1, 0x0000110E,
    0x00001107, 0x000500C7, 0x0000000D, 0x00000DD4, 0x0000110E, 0x00001108,
    0x000500C4, 0x0000000D, 0x00000DD6, 0x00000DD4, 0x00001109, 0x000500C5,
    0x0000000D, 0x00000DD7, 0x00000DD1, 0x00000DD6, 0x000500C2, 0x0000000D,
    0x00000DDA, 0x0000110E, 0x00001109, 0x000500C7, 0x0000000D, 0x00000DDC,
    0x00000DDA, 0x00001108, 0x000500C5, 0x0000000D, 0x00000DDD, 0x00000DD7,
    0x00000DDC, 0x000200F9, 0x00000D98, 0x000200F8, 0x00000D8E, 0x000500C7,
    0x0000000D, 0x00000D9E, 0x000010E8, 0x00001104, 0x000500C7, 0x0000000D,
    0x00000DA1, 0x000010E8, 0x00001105, 0x000500C4, 0x0000000D, 0x00000DA3,
    0x00000DA1, 0x00001106, 0x000500C5, 0x0000000D, 0x00000DA4, 0x00000D9E,
    0x00000DA3, 0x000500C2, 0x0000000D, 0x00000DA7, 0x000010E8, 0x00001106,
    0x000500C7, 0x0000000D, 0x00000DA9, 0x00000DA7, 0x00001105, 0x000500C5,
    0x0000000D, 0x00000DAA, 0x00000DA4, 0x00000DA9, 0x000500C7, 0x0000000D,
    0x00000DAF, 0x0000110E, 0x00001104, 0x000500C7, 0x0000000D, 0x00000DB2,
    0x0000110E, 0x00001105, 0x000500C4, 0x0000000D, 0x00000DB4, 0x00000DB2,
    0x00001106, 0x000500C5, 0x0000000D, 0x00000DB5, 0x00000DAF, 0x00000DB4,
    0x000500C2, 0x0000000D, 0x00000DB8, 0x0000110E, 0x00001106, 0x000500C7,
    0x0000000D, 0x00000DBA, 0x00000DB8, 0x00001105, 0x000500C5, 0x0000000D,
    0x00000DBB, 0x00000DB5, 0x00000DBA, 0x000200F9, 0x00000D98, 0x000200F8,
    0x00000D98, 0x000900F5, 0x0000000D, 0x000010F4, 0x0000110E, 0x00000D8B,
    0x00000DBB, 0x00000D8E, 0x00000DDD, 0x00000D93, 0x000900F5, 0x0000000D,
    0x000010F2, 0x000010E8, 0x00000D8B, 0x00000DAA, 0x00000D8E, 0x00000DCC,
    0x00000D93, 0x000200F9, 0x00000D99, 0x000200F8, 0x00000D99, 0x000700F5,
    0x0000000D, 0x000010F3, 0x0000110E, 0x00000D83, 0x000010F4, 0x00000D98,
    0x000700F5, 0x0000000D, 0x000010F1, 0x000010E8, 0x00000D83, 0x000010F2,
    0x00000D98, 0x00050080, 0x00000008, 0x00000DE4, 0x00001047, 0x000005DE,
    0x000300F7, 0x00000DF8, 0x00000002, 0x000400FA, 0x000005B6, 0x00000DE7,
    0x00000DF2, 0x000200F8, 0x00000DF2, 0x0004007C, 0x0000001C, 0x00000DF4,
    0x00000DE4, 0x00050051, 0x00000014, 0x00000E4D, 0x00000DF4, 0x00000001,
    0x000500C3, 0x00000014, 0x00000E4E, 0x00000E4D, 0x000000D3, 0x0004007C,
    0x00000014, 0x00000E4F, 0x000005CE, 0x00050084, 0x00000014, 0x00000E50,
    0x00000E4E, 0x00000E4F, 0x00050051, 0x00000014, 0x00000E51, 0x00000DF4,
    0x00000000, 0x000500C3, 0x00000014, 0x00000E52, 0x00000E51, 0x000000D3,
    0x00050080, 0x00000014, 0x00000E53, 0x00000E50, 0x00000E52, 0x000500C4,
    0x00000014, 0x00000E54, 0x00000E53, 0x000000C7, 0x000500C3, 0x00000014,
    0x00000E56, 0x00000E4D, 0x000000D1, 0x000500C7, 0x00000014, 0x00000E57,
    0x00000E56, 0x000000D7, 0x000500C4, 0x00000014, 0x00000E58, 0x00000E57,
    0x000000F1, 0x000500C7, 0x00000014, 0x00000E5A, 0x00000E51, 0x000000D7,
    0x000500C5, 0x00000014, 0x00000E5B, 0x00000E58, 0x00000E5A, 0x000500C5,
    0x00000014, 0x00000E5E, 0x00000E54, 0x00000E5B, 0x000500C4, 0x00000014,
    0x00000E5F, 0x00000E5E, 0x0000009F, 0x000500C3, 0x00000014, 0x00000E61,
    0x00000E4D, 0x000000C5, 0x000500C7, 0x00000014, 0x00000E62, 0x00000E61,
    0x000000D1, 0x000500C3, 0x00000014, 0x00000E64, 0x00000E51, 0x000000F1,
    0x000500C7, 0x00000014, 0x00000E65, 0x00000E64, 0x000000F1, 0x000500C3,
    0x00000014, 0x00000E67, 0x00000E4D, 0x000000F1, 0x000500C7, 0x00000014,
    0x00000E68, 0x00000E67, 0x000000D1, 0x000500C4, 0x00000014, 0x00000E69,
    0x00000E68, 0x000000D1, 0x000500C6, 0x00000014, 0x00000E6A, 0x00000E65,
    0x00000E69, 0x000500C7, 0x00000014, 0x00000E6F, 0x00000E4D, 0x000000D1,
    0x000500C4, 0x00000014, 0x00000E73, 0x00000E6F, 0x000000C5, 0x000500C4,
    0x00000014, 0x00000E74, 0x00000E6A, 0x000000C7, 0x000500C5, 0x00000014,
    0x00000E75, 0x00000E73, 0x00000E74, 0x000500C4, 0x00000014, 0x00000E76,
    0x00000E62, 0x000000CA, 0x000500C5, 0x00000014, 0x00000E77, 0x00000E75,
    0x00000E76, 0x000500C7, 0x00000014, 0x00000E78, 0x00000E5F, 0x000000CD,
    0x000500C5, 0x00000014, 0x00000E79, 0x00000E77, 0x00000E78, 0x000500C3,
    0x00000014, 0x00000E7A, 0x00000E5F, 0x000000C5, 0x000500C7, 0x00000014,
    0x00000E7B, 0x00000E7A, 0x000000D1, 0x000500C4, 0x00000014, 0x00000E7C,
    0x00000E7B, 0x000000D3, 0x000500C5, 0x00000014, 0x00000E7D, 0x00000E79,
    0x00000E7C, 0x000500C3, 0x00000014, 0x00000E7E, 0x00000E5F, 0x000000D3,
    0x000500C7, 0x00000014, 0x00000E7F, 0x00000E7E, 0x000000D7, 0x000500C4,
    0x00000014, 0x00000E80, 0x00000E7F, 0x000000D9, 0x000500C5, 0x00000014,
    0x00000E81, 0x00000E7D, 0x00000E80, 0x000500C3, 0x00000014, 0x00000E82,
    0x00000E5F, 0x000000D9, 0x000500C4, 0x00000014, 0x00000E83, 0x00000E82,
    0x000000DD, 0x000500C5, 0x00000014, 0x00000E84, 0x00000E81, 0x00000E83,
    0x0004007C, 0x00000006, 0x00000DF7, 0x00000E84, 0x000200F9, 0x00000DF8,
    0x000200F8, 0x00000DE7, 0x00050051, 0x00000006, 0x00000DEA, 0x00000DE4,
    0x00000000, 0x00050051, 0x00000006, 0x00000DEB, 0x00000DE4, 0x00000001,
    0x00060050, 0x000001F7, 0x00000DEC, 0x00000DEA, 0x00000DEB, 0x000005BA,
    0x0004007C, 0x00000023, 0x00000DED, 0x00000DEC, 0x00050051, 0x00000014,
    0x00000E04, 0x00000DED, 0x00000002, 0x000500C3, 0x00000014, 0x00000E05,
    0x00000E04, 0x00000112, 0x0004007C, 0x00000014, 0x00000E06, 0x000005D3,
    0x00050084, 0x00000014, 0x00000E07, 0x00000E05, 0x00000E06, 0x00050051,
    0x00000014, 0x00000E08, 0x00000DED, 0x00000001, 0x000500C3, 0x00000014,
    0x00000E09, 0x00000E08, 0x000000C5, 0x00050080, 0x00000014, 0x00000E0A,
    0x00000E07, 0x00000E09, 0x0004007C, 0x00000014, 0x00000E0B, 0x000005CE,
    0x00050084, 0x00000014, 0x00000E0C, 0x00000E0A, 0x00000E0B, 0x00050051,
    0x00000014, 0x00000E0D, 0x00000DED, 0x00000000, 0x000500C3, 0x00000014,
    0x00000E0E, 0x00000E0D, 0x000000D3, 0x00050080, 0x00000014, 0x00000E0F,
    0x00000E0C, 0x00000E0E, 0x000500C4, 0x00000014, 0x00000E10, 0x00000E0F,
    0x000000D7, 0x000500C7, 0x00000014, 0x00000E12, 0x00000E04, 0x000000F1,
    0x000500C4, 0x00000014, 0x00000E13, 0x00000E12, 0x000000D3, 0x000500C3,
    0x00000014, 0x00000E15, 0x00000E08, 0x000000D1, 0x000500C7, 0x00000014,
    0x00000E16, 0x00000E15, 0x000000F1, 0x000500C4, 0x00000014, 0x00000E17,
    0x00000E16, 0x000000F1, 0x000500C5, 0x00000014, 0x00000E18, 0x00000E13,
    0x00000E17, 0x000500C7, 0x00000014, 0x00000E1A, 0x00000E0D, 0x000000D7,
    0x000500C5, 0x00000014, 0x00000E1B, 0x00000E18, 0x00000E1A, 0x000500C5,
    0x00000014, 0x00000E1E, 0x00000E10, 0x00000E1B, 0x000500C4, 0x00000014,
    0x00000E1F, 0x00000E1E, 0x0000009F, 0x000500C3, 0x00000014, 0x00000E21,
    0x00000E08, 0x000000F1, 0x000500C6, 0x00000014, 0x00000E24, 0x00000E21,
    0x00000E05, 0x000500C7, 0x00000014, 0x00000E25, 0x00000E24, 0x000000D1,
    0x000500C3, 0x00000014, 0x00000E27, 0x00000E0D, 0x000000F1, 0x000500C7,
    0x00000014, 0x00000E28, 0x00000E27, 0x000000F1, 0x000500C4, 0x00000014,
    0x00000E2A, 0x00000E25, 0x000000D1, 0x000500C6, 0x00000014, 0x00000E2B,
    0x00000E28, 0x00000E2A, 0x000500C7, 0x00000014, 0x00000E30, 0x00000E08,
    0x000000D1, 0x000500C4, 0x00000014, 0x00000E34, 0x00000E30, 0x000000C5,
    0x000500C4, 0x00000014, 0x00000E35, 0x00000E2B, 0x000000C7, 0x000500C5,
    0x00000014, 0x00000E36, 0x00000E34, 0x00000E35, 0x000500C4, 0x00000014,
    0x00000E37, 0x00000E25, 0x000000CA, 0x000500C5, 0x00000014, 0x00000E38,
    0x00000E36, 0x00000E37, 0x000500C7, 0x00000014, 0x00000E39, 0x00000E1F,
    0x000000CD, 0x000500C5, 0x00000014, 0x00000E3A, 0x00000E38, 0x00000E39,
    0x000500C3, 0x00000014, 0x00000E3B, 0x00000E1F, 0x000000C5, 0x000500C7,
    0x00000014, 0x00000E3C, 0x00000E3B, 0x000000D1, 0x000500C4, 0x00000014,
    0x00000E3D, 0x00000E3C, 0x000000D3, 0x000500C5, 0x00000014, 0x00000E3E,
    0x00000E3A, 0x00000E3D, 0x000500C3, 0x00000014, 0x00000E3F, 0x00000E1F,
    0x000000D3, 0x000500C7, 0x00000014, 0x00000E40, 0x00000E3F, 0x000000D7,
    0x000500C4, 0x00000014, 0x00000E41, 0x00000E40, 0x000000D9, 0x000500C5,
    0x00000014, 0x00000E42, 0x00000E3E, 0x00000E41, 0x000500C3, 0x00000014,
    0x00000E43, 0x00000E1F, 0x000000D9, 0x000500C4, 0x00000014, 0x00000E44,
    0x00000E43, 0x000000DD, 0x000500C5, 0x00000014, 0x00000E45, 0x00000E42,
    0x00000E44, 0x0004007C, 0x00000006, 0x00000DF1, 0x00000E45, 0x000200F9,
    0x00000DF8, 0x000200F8, 0x00000DF8, 0x000700F5, 0x00000006, 0x000010F5,
    0x00000DF1, 0x00000DE7, 0x00000DF7, 0x00000DF2, 0x00050080, 0x00000006,
    0x00000DFB, 0x000010F5, 0x000005E5, 0x000500C2, 0x00000006, 0x0000055C,
    0x00000DFB, 0x000000C5, 0x000500AA, 0x00000030, 0x00000E88, 0x000005B2,
    0x0000009C, 0x000500AA, 0x00000030, 0x00000E8A, 0x000005B2, 0x0000009F,
    0x000500A6, 0x00000030, 0x00000E8B, 0x00000E88, 0x00000E8A, 0x000300F7,
    0x00000E98, 0x00000000, 0x000400FA, 0x00000E8B, 0x00000E8C, 0x00000E98,
    0x000200F8, 0x00000E8C, 0x000500C7, 0x0000000D, 0x00000E8F, 0x000010F1,
    0x0000110A, 0x000500C4, 0x0000000D, 0x00000E91, 0x00000E8F, 0x0000110B,
    0x000500C7, 0x0000000D, 0x00000E94, 0x000010F1, 0x00001104, 0x000500C2,
    0x0000000D, 0x00000E96, 0x00000E94, 0x0000110B, 0x000500C5, 0x0000000D,
    0x00000E97, 0x00000E91, 0x00000E96, 0x000200F9, 0x00000E98, 0x000200F8,
    0x00000E98, 0x000700F5, 0x0000000D, 0x000010F7, 0x000010F1, 0x00000DF8,
    0x00000E97, 0x00000E8C, 0x000500AA, 0x00000030, 0x00000E9C, 0x000005B2,
    0x000000B5, 0x000500A6, 0x00000030, 0x00000E9D, 0x00000E8A, 0x00000E9C,
    0x000300F7, 0x00000EA6, 0x00000000, 0x000400FA, 0x00000E9D, 0x00000E9E,
    0x00000EA6, 0x000200F8, 0x00000E9E, 0x000500C4, 0x0000000D, 0x00000EA1,
    0x000010F7, 0x00001106, 0x000500C2, 0x0000000D, 0x00000EA4, 0x000010F7,
    0x00001106, 0x000500C5, 0x0000000D, 0x00000EA5, 0x00000EA1, 0x00000EA4,
    0x000200F9, 0x00000EA6, 0x000200F8, 0x00000EA6, 0x000700F5, 0x0000000D,
    0x000010F8, 0x000010F7, 0x00000E98, 0x00000EA5, 0x00000E9E, 0x00060041,
    0x0000048B, 0x00000561, 0x00000482, 0x00000148, 0x0000055C, 0x0003003E,
    0x00000561, 0x000010F8, 0x00050080, 0x00000006, 0x00000564, 0x00000DFB,
    0x00001111, 0x000500C2, 0x00000006, 0x00000566, 0x00000564, 0x000000C5,
    0x000300F7, 0x00000EDB, 0x00000000, 0x000400FA, 0x00000E8B, 0x00000ECF,
    0x00000EDB, 0x000200F8, 0x00000ECF, 0x000500C7, 0x0000000D, 0x00000ED2,
    0x000010F3, 0x0000110A, 0x000500C4, 0x0000000D, 0x00000ED4, 0x00000ED2,
    0x0000110B, 0x000500C7, 0x0000000D, 0x00000ED7, 0x000010F3, 0x00001104,
    0x000500C2, 0x0000000D, 0x00000ED9, 0x00000ED7, 0x0000110B, 0x000500C5,
    0x0000000D, 0x00000EDA, 0x00000ED4, 0x00000ED9, 0x000200F9, 0x00000EDB,
    0x000200F8, 0x00000EDB, 0x000700F5, 0x0000000D, 0x000010FE, 0x000010F3,
    0x00000EA6, 0x00000EDA, 0x00000ECF, 0x000300F7, 0x00000EE9, 0x00000000,
    0x000400FA, 0x00000E9D, 0x00000EE1, 0x00000EE9, 0x000200F8, 0x00000EE1,
    0x000500C4, 0x0000000D, 0x00000EE4, 0x000010FE, 0x00001106, 0x000500C2,
    0x0000000D, 0x00000EE7, 0x000010FE, 0x00001106, 0x000500C5, 0x0000000D,
    0x00000EE8, 0x00000EE4, 0x00000EE7, 0x000200F9, 0x00000EE9, 0x000200F8,
    0x00000EE9, 0x000700F5, 0x0000000D, 0x000010FF, 0x000010FE, 0x00000EDB,
    0x00000EE8, 0x00000EE1, 0x00060041, 0x0000048B, 0x0000056B, 0x00000482,
    0x00000148, 0x00000566, 0x0003003E, 0x0000056B, 0x000010FF, 0x000200F9,
    0x0000056C, 0x000200F8, 0x0000056C, 0x000100FD, 0x00010038,
};
