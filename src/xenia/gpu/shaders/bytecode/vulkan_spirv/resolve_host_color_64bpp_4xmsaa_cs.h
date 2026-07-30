// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.3
; Generator: Google Shaderc over Glslang; 10
; Bound: 3521
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
        %402 = OpConstantComposite %v2uint %uint_0 %uint_4
        %406 = OpConstantComposite %v2uint %uint_4 %uint_1
     %uint_5 = OpConstant %uint 5
     %uint_7 = OpConstant %uint 7
    %float_1 = OpConstant %float 1
%uint_16777216 = OpConstant %uint 16777216
    %uint_20 = OpConstant %uint 20
        %484 = OpConstantComposite %v2uint %uint_20 %uint_24
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
        %763 = OpConstantComposite %v2uint %uint_1 %uint_0
        %784 = OpTypeImage %float 2D 0 0 1 1 Unknown
%_ptr_UniformConstant_784 = OpTypePointer UniformConstant %784
%xe_resolve_host_color_source = OpVariable %_ptr_UniformConstant_784 UniformConstant
      %false = OpConstantFalse %bool
       %true = OpConstantTrue %bool
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%xe_resolve_dest_xe_block = OpTypeStruct %_runtimearr_v4uint
%_ptr_StorageBuffer_xe_resolve_dest_xe_block = OpTypePointer StorageBuffer %xe_resolve_dest_xe_block
%xe_resolve_dest = OpVariable %_ptr_StorageBuffer_xe_resolve_dest_xe_block StorageBuffer
%_ptr_StorageBuffer_v4uint = OpTypePointer StorageBuffer %v4uint
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %3447 = OpUndef %v2uint
       %3501 = OpConstantComposite %v2uint %uint_1 %uint_1
       %3503 = OpConstantComposite %v2uint %uint_3 %uint_3
       %3504 = OpConstantComposite %v2uint %uint_15 %uint_15
       %3505 = OpConstantComposite %v4uint %uint_4294901760 %uint_4294901760 %uint_4294901760 %uint_4294901760
       %3506 = OpConstantComposite %v4uint %uint_65535 %uint_65535 %uint_65535 %uint_65535
       %3507 = OpConstantComposite %v4uint %uint_16711935 %uint_16711935 %uint_16711935 %uint_16711935
       %3508 = OpConstantComposite %v4uint %uint_8 %uint_8 %uint_8 %uint_8
       %3509 = OpConstantComposite %v4uint %uint_4278255360 %uint_4278255360 %uint_4278255360 %uint_4278255360
       %3510 = OpConstantComposite %v4uint %uint_16 %uint_16 %uint_16 %uint_16
    %uint_32 = OpConstant %uint 32
       %main = OpFunction %void None %3
          %5 = OpLabel
       %1167 = OpLoad %v3uint %gl_GlobalInvocationID
               OpSelectionMerge %1332 None
               OpSwitch %uint_0 %1240
       %1240 = OpLabel
       %1345 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_0
       %1346 = OpLoad %uint %1345
       %1347 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_1
       %1348 = OpLoad %uint %1347
       %1365 = OpShiftRightLogical %uint %1346 %uint_24
       %1366 = OpBitwiseAnd %uint %1365 %uint_15
       %1470 = OpCompositeConstruct %v2uint %1348 %1348
       %1378 = OpShiftRightLogical %v2uint %1470 %402
       %1380 = OpShiftLeftLogical %v2uint %3501 %406
       %1382 = OpISub %v2uint %1380 %3501
       %1383 = OpBitwiseAnd %v2uint %1378 %1382
       %1385 = OpShiftLeftLogical %v2uint %1383 %3503
       %1388 = OpIMul %v2uint %1385 %3501
       %1391 = OpShiftRightLogical %uint %1348 %uint_5
       %1392 = OpBitwiseAnd %uint %1391 %uint_2047
       %1397 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_2
       %1398 = OpLoad %uint %1397
       %1399 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_3
       %1400 = OpLoad %uint %1399
       %1402 = OpBitwiseAnd %uint %1398 %uint_7
       %1405 = OpBitwiseAnd %uint %1398 %uint_8
       %1406 = OpINotEqual %bool %1405 %uint_0
       %1409 = OpShiftRightLogical %uint %1398 %uint_4
       %1410 = OpBitwiseAnd %uint %1409 %uint_7
       %1426 = OpBitwiseAnd %uint %1398 %uint_16777216
       %1427 = OpINotEqual %bool %1426 %uint_0
       %1430 = OpBitwiseAnd %uint %1400 %uint_1023
       %1433 = OpShiftRightLogical %uint %1400 %uint_10
       %1434 = OpBitwiseAnd %uint %1433 %uint_1023
       %1435 = OpShiftLeftLogical %uint %1434 %int_1
       %1480 = OpCompositeConstruct %v2uint %1400 %1400
       %1439 = OpShiftRightLogical %v2uint %1480 %484
       %1441 = OpBitwiseAnd %v2uint %1439 %3504
       %1443 = OpShiftLeftLogical %v2uint %1441 %3503
       %1446 = OpIMul %v2uint %1443 %3501
       %1449 = OpShiftRightLogical %uint %1400 %uint_28
       %1450 = OpBitwiseAnd %uint %1449 %uint_7
       %1452 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_4
       %1453 = OpLoad %uint %1452
               OpSelectionMerge %1612 None
               OpSwitch %uint_0 %1501
       %1501 = OpLabel
       %1503 = OpCompositeExtract %uint %1167 0
       %1504 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_5
       %1505 = OpLoad %uint %1504
       %1506 = OpUGreaterThanEqual %bool %1503 %1505
       %1507 = OpLogicalNot %bool %1506
               OpSelectionMerge %1514 None
               OpBranchConditional %1507 %1508 %1514
       %1508 = OpLabel
       %1510 = OpCompositeExtract %uint %1167 1
       %1511 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_6
       %1512 = OpLoad %uint %1511
       %1513 = OpUGreaterThanEqual %bool %1510 %1512
               OpBranch %1514
       %1514 = OpLabel
       %1515 = OpPhi %bool %1506 %1501 %1513 %1508
               OpSelectionMerge %1517 None
               OpBranchConditional %1515 %1516 %1517
       %1516 = OpLabel
               OpBranch %1612
       %1517 = OpLabel
       %1526 = OpIMul %uint %1503 %uint_4
       %1528 = OpCompositeExtract %uint %1167 1
       %1531 = OpUDiv %uint %1526 %uint_20
       %1534 = OpUDiv %uint %1528 %uint_8
       %1538 = OpIMul %uint %1531 %uint_20
       %1539 = OpISub %uint %1526 %1538
       %1543 = OpIMul %uint %1534 %uint_8
       %1544 = OpISub %uint %1528 %1543
       %1545 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_0
       %1546 = OpLoad %uint %1545
       %1548 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_2
       %1549 = OpLoad %uint %1548
       %1550 = OpIMul %uint %1534 %1549
       %1551 = OpIAdd %uint %1546 %1550
       %1553 = OpIAdd %uint %1551 %1531
       %1558 = OpUDiv %uint %1553 %1549
       %1562 = OpIMul %uint %1558 %1549
       %1563 = OpISub %uint %1553 %1562
       %1566 = OpIMul %uint %1563 %uint_20
       %1568 = OpIAdd %uint %1566 %1539
       %1571 = OpIMul %uint %1558 %uint_8
       %1573 = OpIAdd %uint %1571 %1544
       %1574 = OpCompositeConstruct %v2uint %1568 %1573
       %1578 = OpCompositeExtract %uint %1388 0
       %1579 = OpULessThan %bool %1568 %1578
       %1580 = OpLogicalNot %bool %1579
               OpSelectionMerge %1587 None
               OpBranchConditional %1580 %1581 %1587
       %1581 = OpLabel
       %1585 = OpCompositeExtract %uint %1388 1
       %1586 = OpULessThan %bool %1573 %1585
               OpBranch %1587
       %1587 = OpLabel
       %1588 = OpPhi %bool %1579 %1517 %1586 %1581
               OpSelectionMerge %1590 None
               OpBranchConditional %1588 %1589 %1590
       %1589 = OpLabel
               OpBranch %1612
       %1590 = OpLabel
       %1594 = OpISub %v2uint %1574 %1388
       %1596 = OpCompositeExtract %uint %1594 0
       %1599 = OpShiftLeftLogical %uint %1392 %uint_3
       %1600 = OpUGreaterThanEqual %bool %1596 %1599
       %1601 = OpLogicalNot %bool %1600
               OpSelectionMerge %1608 None
               OpBranchConditional %1601 %1602 %1608
       %1602 = OpLabel
       %1604 = OpCompositeExtract %uint %1594 1
       %1605 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_7
       %1606 = OpLoad %uint %1605
       %1607 = OpUGreaterThanEqual %bool %1604 %1606
               OpBranch %1608
       %1608 = OpLabel
       %1609 = OpPhi %bool %1600 %1590 %1607 %1602
               OpSelectionMerge %1611 None
               OpBranchConditional %1609 %1610 %1611
       %1610 = OpLabel
               OpBranch %1612
       %1611 = OpLabel
               OpBranch %1612
       %1612 = OpLabel
       %3445 = OpPhi %v2uint %3447 %1516 %3447 %1589 %1594 %1610 %1594 %1611
       %3444 = OpPhi %bool %false %1516 %false %1589 %false %1610 %true %1611
       %1246 = OpLogicalNot %bool %3444
               OpSelectionMerge %1248 None
               OpBranchConditional %1246 %1247 %1248
       %1247 = OpLabel
               OpBranch %1332
       %1248 = OpLabel
       %1656 = OpULessThanEqual %bool %1450 %uint_3
               OpSelectionMerge %1665 None
               OpBranchConditional %1656 %1657 %1659
       %1659 = OpLabel
       %1661 = OpIEqual %bool %1450 %uint_5
       %3515 = OpSelect %uint %1661 %uint_2 %uint_0
               OpBranch %1665
       %1657 = OpLabel
               OpBranch %1665
       %1665 = OpLabel
       %3450 = OpPhi %uint %1450 %1657 %3515 %1659
       %1673 = OpCompositeExtract %uint %3445 0
       %1677 = OpCompositeExtract %uint %3445 1
       %1680 = OpExtInst %uint %1 UMax %1677 %uint_0
       %1681 = OpCompositeConstruct %v2uint %1673 %1680
       %1684 = OpIAdd %v2uint %1681 %1388
       %1687 = OpShiftLeftLogical %v2uint %1684 %3501
       %1708 = OpCompositeConstruct %v2uint %3450 %3450
       %1701 = OpShiftRightLogical %v2uint %1708 %763
       %1703 = OpBitwiseAnd %v2uint %1701 %3501
       %1690 = OpIAdd %v2uint %1687 %1703
       %1763 = OpCompositeExtract %uint %1690 0
       %1765 = OpUDiv %uint %1763 %uint_40
       %1767 = OpCompositeExtract %uint %1690 1
       %1769 = OpUDiv %uint %1767 %uint_16
       %1774 = OpIMul %uint %1765 %uint_40
       %1775 = OpISub %uint %1763 %1774
       %1780 = OpIMul %uint %1769 %uint_16
       %1781 = OpISub %uint %1767 %1780
       %1783 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_2
       %1784 = OpLoad %uint %1783
       %1785 = OpIMul %uint %1769 %1784
       %1787 = OpIAdd %uint %1785 %1765
       %1788 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_1
       %1789 = OpLoad %uint %1788
       %1791 = OpIAdd %uint %1789 %1787
       %1793 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_3
       %1794 = OpLoad %uint %1793
       %1795 = OpISub %uint %1791 %1794
       %1796 = OpAccessChain %_ptr_Uniform_uint %xe_resolve_host_color %int_4
       %1797 = OpLoad %uint %1796
       %1800 = OpUDiv %uint %1795 %1797
       %1804 = OpIMul %uint %1800 %1797
       %1805 = OpISub %uint %1795 %1804
       %1808 = OpIMul %uint %1805 %uint_40
       %1810 = OpIAdd %uint %1808 %1775
       %1813 = OpIMul %uint %1800 %uint_16
       %1815 = OpIAdd %uint %1813 %1781
       %1720 = OpBitwiseAnd %uint %1810 %uint_1
       %1723 = OpBitwiseAnd %uint %1815 %uint_1
       %1724 = OpShiftLeftLogical %uint %1723 %uint_1
       %1725 = OpBitwiseOr %uint %1720 %1724
       %1726 = OpLoad %784 %xe_resolve_host_color_source
       %1729 = OpShiftRightLogical %uint %1810 %uint_1
       %1730 = OpBitcast %int %1729
       %1733 = OpShiftRightLogical %uint %1815 %uint_1
       %1734 = OpBitcast %int %1733
       %1738 = OpCompositeConstruct %v2int %1730 %1734
       %1740 = OpBitcast %int %1725
       %1741 = OpImageFetch %v4float %1726 %1738 Sample %1740
               OpSelectionMerge %1867 None
               OpSwitch %1366 %1837 5 %1841 7 %1859
       %1859 = OpLabel
       %1861 = OpVectorShuffle %v2float %1741 %1741 0 1
       %1862 = OpExtInst %uint %1 PackHalf2x16 %1861
       %1864 = OpVectorShuffle %v2float %1741 %1741 2 3
       %1865 = OpExtInst %uint %1 PackHalf2x16 %1864
       %1866 = OpCompositeConstruct %v2uint %1862 %1865
               OpBranch %1867
       %1841 = OpLabel
       %1843 = OpCompositeExtract %float %1741 0
       %1877 = OpExtInst %float %1 FMax %1843 %float_n1
       %1878 = OpExtInst %float %1 FMin %1877 %float_1
       %1880 = OpFOrdGreaterThanEqual %bool %1878 %float_0
       %1881 = OpSelect %float %1880 %float_0_5 %float_n0_5
       %1885 = OpExtInst %float %1 Fma %1878 %float_32767 %1881
       %1886 = OpConvertFToS %int %1885
       %1887 = OpBitcast %uint %1886
       %1888 = OpBitwiseAnd %uint %1887 %uint_65535
       %1846 = OpCompositeExtract %float %1741 1
       %1894 = OpExtInst %float %1 FMax %1846 %float_n1
       %1895 = OpExtInst %float %1 FMin %1894 %float_1
       %1897 = OpFOrdGreaterThanEqual %bool %1895 %float_0
       %1898 = OpSelect %float %1897 %float_0_5 %float_n0_5
       %1902 = OpExtInst %float %1 Fma %1895 %float_32767 %1898
       %1903 = OpConvertFToS %int %1902
       %1904 = OpBitcast %uint %1903
       %1905 = OpBitwiseAnd %uint %1904 %uint_65535
       %1848 = OpShiftLeftLogical %uint %1905 %uint_16
       %1849 = OpBitwiseOr %uint %1888 %1848
       %1851 = OpCompositeExtract %float %1741 2
       %1911 = OpExtInst %float %1 FMax %1851 %float_n1
       %1912 = OpExtInst %float %1 FMin %1911 %float_1
       %1914 = OpFOrdGreaterThanEqual %bool %1912 %float_0
       %1915 = OpSelect %float %1914 %float_0_5 %float_n0_5
       %1919 = OpExtInst %float %1 Fma %1912 %float_32767 %1915
       %1920 = OpConvertFToS %int %1919
       %1921 = OpBitcast %uint %1920
       %1922 = OpBitwiseAnd %uint %1921 %uint_65535
       %1854 = OpCompositeExtract %float %1741 3
       %1928 = OpExtInst %float %1 FMax %1854 %float_n1
       %1929 = OpExtInst %float %1 FMin %1928 %float_1
       %1931 = OpFOrdGreaterThanEqual %bool %1929 %float_0
       %1932 = OpSelect %float %1931 %float_0_5 %float_n0_5
       %1936 = OpExtInst %float %1 Fma %1929 %float_32767 %1932
       %1937 = OpConvertFToS %int %1936
       %1938 = OpBitcast %uint %1937
       %1939 = OpBitwiseAnd %uint %1938 %uint_65535
       %1856 = OpShiftLeftLogical %uint %1939 %uint_16
       %1857 = OpBitwiseOr %uint %1922 %1856
       %1858 = OpCompositeConstruct %v2uint %1849 %1857
               OpBranch %1867
       %1837 = OpLabel
       %1839 = OpVectorShuffle %v2float %1741 %1741 0 1
       %1840 = OpBitcast %v2uint %1839
               OpBranch %1867
       %1867 = OpLabel
       %3454 = OpPhi %v2uint %1840 %1837 %1858 %1841 %1866 %1859
               OpSelectionMerge %1967 None
               OpBranchConditional %1656 %1959 %1961
       %1961 = OpLabel
       %1963 = OpIEqual %bool %1450 %uint_5
       %3516 = OpSelect %uint %1963 %uint_2 %uint_0
               OpBranch %1967
       %1959 = OpLabel
               OpBranch %1967
       %1967 = OpLabel
       %3455 = OpPhi %uint %1450 %1959 %3516 %1961
       %1977 = OpIAdd %uint %1673 %uint_1
       %1983 = OpCompositeConstruct %v2uint %1977 %1680
       %1986 = OpIAdd %v2uint %1983 %1388
       %1989 = OpShiftLeftLogical %v2uint %1986 %3501
       %2010 = OpCompositeConstruct %v2uint %3455 %3455
       %2003 = OpShiftRightLogical %v2uint %2010 %763
       %2005 = OpBitwiseAnd %v2uint %2003 %3501
       %1992 = OpIAdd %v2uint %1989 %2005
       %2065 = OpCompositeExtract %uint %1992 0
       %2067 = OpUDiv %uint %2065 %uint_40
       %2069 = OpCompositeExtract %uint %1992 1
       %2071 = OpUDiv %uint %2069 %uint_16
       %2076 = OpIMul %uint %2067 %uint_40
       %2077 = OpISub %uint %2065 %2076
       %2082 = OpIMul %uint %2071 %uint_16
       %2083 = OpISub %uint %2069 %2082
       %2087 = OpIMul %uint %2071 %1784
       %2089 = OpIAdd %uint %2087 %2067
       %2093 = OpIAdd %uint %1789 %2089
       %2097 = OpISub %uint %2093 %1794
       %2102 = OpUDiv %uint %2097 %1797
       %2106 = OpIMul %uint %2102 %1797
       %2107 = OpISub %uint %2097 %2106
       %2110 = OpIMul %uint %2107 %uint_40
       %2112 = OpIAdd %uint %2110 %2077
       %2115 = OpIMul %uint %2102 %uint_16
       %2117 = OpIAdd %uint %2115 %2083
       %2022 = OpBitwiseAnd %uint %2112 %uint_1
       %2025 = OpBitwiseAnd %uint %2117 %uint_1
       %2026 = OpShiftLeftLogical %uint %2025 %uint_1
       %2027 = OpBitwiseOr %uint %2022 %2026
       %2031 = OpShiftRightLogical %uint %2112 %uint_1
       %2032 = OpBitcast %int %2031
       %2035 = OpShiftRightLogical %uint %2117 %uint_1
       %2036 = OpBitcast %int %2035
       %2040 = OpCompositeConstruct %v2int %2032 %2036
       %2042 = OpBitcast %int %2027
       %2043 = OpImageFetch %v4float %1726 %2040 Sample %2042
               OpSelectionMerge %2169 None
               OpSwitch %1366 %2139 5 %2143 7 %2161
       %2161 = OpLabel
       %2163 = OpVectorShuffle %v2float %2043 %2043 0 1
       %2164 = OpExtInst %uint %1 PackHalf2x16 %2163
       %2166 = OpVectorShuffle %v2float %2043 %2043 2 3
       %2167 = OpExtInst %uint %1 PackHalf2x16 %2166
       %2168 = OpCompositeConstruct %v2uint %2164 %2167
               OpBranch %2169
       %2143 = OpLabel
       %2145 = OpCompositeExtract %float %2043 0
       %2179 = OpExtInst %float %1 FMax %2145 %float_n1
       %2180 = OpExtInst %float %1 FMin %2179 %float_1
       %2182 = OpFOrdGreaterThanEqual %bool %2180 %float_0
       %2183 = OpSelect %float %2182 %float_0_5 %float_n0_5
       %2187 = OpExtInst %float %1 Fma %2180 %float_32767 %2183
       %2188 = OpConvertFToS %int %2187
       %2189 = OpBitcast %uint %2188
       %2190 = OpBitwiseAnd %uint %2189 %uint_65535
       %2148 = OpCompositeExtract %float %2043 1
       %2196 = OpExtInst %float %1 FMax %2148 %float_n1
       %2197 = OpExtInst %float %1 FMin %2196 %float_1
       %2199 = OpFOrdGreaterThanEqual %bool %2197 %float_0
       %2200 = OpSelect %float %2199 %float_0_5 %float_n0_5
       %2204 = OpExtInst %float %1 Fma %2197 %float_32767 %2200
       %2205 = OpConvertFToS %int %2204
       %2206 = OpBitcast %uint %2205
       %2207 = OpBitwiseAnd %uint %2206 %uint_65535
       %2150 = OpShiftLeftLogical %uint %2207 %uint_16
       %2151 = OpBitwiseOr %uint %2190 %2150
       %2153 = OpCompositeExtract %float %2043 2
       %2213 = OpExtInst %float %1 FMax %2153 %float_n1
       %2214 = OpExtInst %float %1 FMin %2213 %float_1
       %2216 = OpFOrdGreaterThanEqual %bool %2214 %float_0
       %2217 = OpSelect %float %2216 %float_0_5 %float_n0_5
       %2221 = OpExtInst %float %1 Fma %2214 %float_32767 %2217
       %2222 = OpConvertFToS %int %2221
       %2223 = OpBitcast %uint %2222
       %2224 = OpBitwiseAnd %uint %2223 %uint_65535
       %2156 = OpCompositeExtract %float %2043 3
       %2230 = OpExtInst %float %1 FMax %2156 %float_n1
       %2231 = OpExtInst %float %1 FMin %2230 %float_1
       %2233 = OpFOrdGreaterThanEqual %bool %2231 %float_0
       %2234 = OpSelect %float %2233 %float_0_5 %float_n0_5
       %2238 = OpExtInst %float %1 Fma %2231 %float_32767 %2234
       %2239 = OpConvertFToS %int %2238
       %2240 = OpBitcast %uint %2239
       %2241 = OpBitwiseAnd %uint %2240 %uint_65535
       %2158 = OpShiftLeftLogical %uint %2241 %uint_16
       %2159 = OpBitwiseOr %uint %2224 %2158
       %2160 = OpCompositeConstruct %v2uint %2151 %2159
               OpBranch %2169
       %2139 = OpLabel
       %2141 = OpVectorShuffle %v2float %2043 %2043 0 1
       %2142 = OpBitcast %v2uint %2141
               OpBranch %2169
       %2169 = OpLabel
       %3459 = OpPhi %v2uint %2142 %2139 %2160 %2143 %2168 %2161
               OpSelectionMerge %2269 None
               OpBranchConditional %1656 %2261 %2263
       %2263 = OpLabel
       %2265 = OpIEqual %bool %1450 %uint_5
       %3517 = OpSelect %uint %2265 %uint_2 %uint_0
               OpBranch %2269
       %2261 = OpLabel
               OpBranch %2269
       %2269 = OpLabel
       %3460 = OpPhi %uint %1450 %2261 %3517 %2263
       %2279 = OpIAdd %uint %1673 %uint_2
       %2285 = OpCompositeConstruct %v2uint %2279 %1680
       %2288 = OpIAdd %v2uint %2285 %1388
       %2291 = OpShiftLeftLogical %v2uint %2288 %3501
       %2312 = OpCompositeConstruct %v2uint %3460 %3460
       %2305 = OpShiftRightLogical %v2uint %2312 %763
       %2307 = OpBitwiseAnd %v2uint %2305 %3501
       %2294 = OpIAdd %v2uint %2291 %2307
       %2367 = OpCompositeExtract %uint %2294 0
       %2369 = OpUDiv %uint %2367 %uint_40
       %2371 = OpCompositeExtract %uint %2294 1
       %2373 = OpUDiv %uint %2371 %uint_16
       %2378 = OpIMul %uint %2369 %uint_40
       %2379 = OpISub %uint %2367 %2378
       %2384 = OpIMul %uint %2373 %uint_16
       %2385 = OpISub %uint %2371 %2384
       %2389 = OpIMul %uint %2373 %1784
       %2391 = OpIAdd %uint %2389 %2369
       %2395 = OpIAdd %uint %1789 %2391
       %2399 = OpISub %uint %2395 %1794
       %2404 = OpUDiv %uint %2399 %1797
       %2408 = OpIMul %uint %2404 %1797
       %2409 = OpISub %uint %2399 %2408
       %2412 = OpIMul %uint %2409 %uint_40
       %2414 = OpIAdd %uint %2412 %2379
       %2417 = OpIMul %uint %2404 %uint_16
       %2419 = OpIAdd %uint %2417 %2385
       %2324 = OpBitwiseAnd %uint %2414 %uint_1
       %2327 = OpBitwiseAnd %uint %2419 %uint_1
       %2328 = OpShiftLeftLogical %uint %2327 %uint_1
       %2329 = OpBitwiseOr %uint %2324 %2328
       %2333 = OpShiftRightLogical %uint %2414 %uint_1
       %2334 = OpBitcast %int %2333
       %2337 = OpShiftRightLogical %uint %2419 %uint_1
       %2338 = OpBitcast %int %2337
       %2342 = OpCompositeConstruct %v2int %2334 %2338
       %2344 = OpBitcast %int %2329
       %2345 = OpImageFetch %v4float %1726 %2342 Sample %2344
               OpSelectionMerge %2471 None
               OpSwitch %1366 %2441 5 %2445 7 %2463
       %2463 = OpLabel
       %2465 = OpVectorShuffle %v2float %2345 %2345 0 1
       %2466 = OpExtInst %uint %1 PackHalf2x16 %2465
       %2468 = OpVectorShuffle %v2float %2345 %2345 2 3
       %2469 = OpExtInst %uint %1 PackHalf2x16 %2468
       %2470 = OpCompositeConstruct %v2uint %2466 %2469
               OpBranch %2471
       %2445 = OpLabel
       %2447 = OpCompositeExtract %float %2345 0
       %2481 = OpExtInst %float %1 FMax %2447 %float_n1
       %2482 = OpExtInst %float %1 FMin %2481 %float_1
       %2484 = OpFOrdGreaterThanEqual %bool %2482 %float_0
       %2485 = OpSelect %float %2484 %float_0_5 %float_n0_5
       %2489 = OpExtInst %float %1 Fma %2482 %float_32767 %2485
       %2490 = OpConvertFToS %int %2489
       %2491 = OpBitcast %uint %2490
       %2492 = OpBitwiseAnd %uint %2491 %uint_65535
       %2450 = OpCompositeExtract %float %2345 1
       %2498 = OpExtInst %float %1 FMax %2450 %float_n1
       %2499 = OpExtInst %float %1 FMin %2498 %float_1
       %2501 = OpFOrdGreaterThanEqual %bool %2499 %float_0
       %2502 = OpSelect %float %2501 %float_0_5 %float_n0_5
       %2506 = OpExtInst %float %1 Fma %2499 %float_32767 %2502
       %2507 = OpConvertFToS %int %2506
       %2508 = OpBitcast %uint %2507
       %2509 = OpBitwiseAnd %uint %2508 %uint_65535
       %2452 = OpShiftLeftLogical %uint %2509 %uint_16
       %2453 = OpBitwiseOr %uint %2492 %2452
       %2455 = OpCompositeExtract %float %2345 2
       %2515 = OpExtInst %float %1 FMax %2455 %float_n1
       %2516 = OpExtInst %float %1 FMin %2515 %float_1
       %2518 = OpFOrdGreaterThanEqual %bool %2516 %float_0
       %2519 = OpSelect %float %2518 %float_0_5 %float_n0_5
       %2523 = OpExtInst %float %1 Fma %2516 %float_32767 %2519
       %2524 = OpConvertFToS %int %2523
       %2525 = OpBitcast %uint %2524
       %2526 = OpBitwiseAnd %uint %2525 %uint_65535
       %2458 = OpCompositeExtract %float %2345 3
       %2532 = OpExtInst %float %1 FMax %2458 %float_n1
       %2533 = OpExtInst %float %1 FMin %2532 %float_1
       %2535 = OpFOrdGreaterThanEqual %bool %2533 %float_0
       %2536 = OpSelect %float %2535 %float_0_5 %float_n0_5
       %2540 = OpExtInst %float %1 Fma %2533 %float_32767 %2536
       %2541 = OpConvertFToS %int %2540
       %2542 = OpBitcast %uint %2541
       %2543 = OpBitwiseAnd %uint %2542 %uint_65535
       %2460 = OpShiftLeftLogical %uint %2543 %uint_16
       %2461 = OpBitwiseOr %uint %2526 %2460
       %2462 = OpCompositeConstruct %v2uint %2453 %2461
               OpBranch %2471
       %2441 = OpLabel
       %2443 = OpVectorShuffle %v2float %2345 %2345 0 1
       %2444 = OpBitcast %v2uint %2443
               OpBranch %2471
       %2471 = OpLabel
       %3464 = OpPhi %v2uint %2444 %2441 %2462 %2445 %2470 %2463
               OpSelectionMerge %2571 None
               OpBranchConditional %1656 %2563 %2565
       %2565 = OpLabel
       %2567 = OpIEqual %bool %1450 %uint_5
       %3518 = OpSelect %uint %2567 %uint_2 %uint_0
               OpBranch %2571
       %2563 = OpLabel
               OpBranch %2571
       %2571 = OpLabel
       %3465 = OpPhi %uint %1450 %2563 %3518 %2565
       %2581 = OpIAdd %uint %1673 %uint_3
       %2587 = OpCompositeConstruct %v2uint %2581 %1680
       %2590 = OpIAdd %v2uint %2587 %1388
       %2593 = OpShiftLeftLogical %v2uint %2590 %3501
       %2614 = OpCompositeConstruct %v2uint %3465 %3465
       %2607 = OpShiftRightLogical %v2uint %2614 %763
       %2609 = OpBitwiseAnd %v2uint %2607 %3501
       %2596 = OpIAdd %v2uint %2593 %2609
       %2669 = OpCompositeExtract %uint %2596 0
       %2671 = OpUDiv %uint %2669 %uint_40
       %2673 = OpCompositeExtract %uint %2596 1
       %2675 = OpUDiv %uint %2673 %uint_16
       %2680 = OpIMul %uint %2671 %uint_40
       %2681 = OpISub %uint %2669 %2680
       %2686 = OpIMul %uint %2675 %uint_16
       %2687 = OpISub %uint %2673 %2686
       %2691 = OpIMul %uint %2675 %1784
       %2693 = OpIAdd %uint %2691 %2671
       %2697 = OpIAdd %uint %1789 %2693
       %2701 = OpISub %uint %2697 %1794
       %2706 = OpUDiv %uint %2701 %1797
       %2710 = OpIMul %uint %2706 %1797
       %2711 = OpISub %uint %2701 %2710
       %2714 = OpIMul %uint %2711 %uint_40
       %2716 = OpIAdd %uint %2714 %2681
       %2719 = OpIMul %uint %2706 %uint_16
       %2721 = OpIAdd %uint %2719 %2687
       %2626 = OpBitwiseAnd %uint %2716 %uint_1
       %2629 = OpBitwiseAnd %uint %2721 %uint_1
       %2630 = OpShiftLeftLogical %uint %2629 %uint_1
       %2631 = OpBitwiseOr %uint %2626 %2630
       %2635 = OpShiftRightLogical %uint %2716 %uint_1
       %2636 = OpBitcast %int %2635
       %2639 = OpShiftRightLogical %uint %2721 %uint_1
       %2640 = OpBitcast %int %2639
       %2644 = OpCompositeConstruct %v2int %2636 %2640
       %2646 = OpBitcast %int %2631
       %2647 = OpImageFetch %v4float %1726 %2644 Sample %2646
               OpSelectionMerge %2773 None
               OpSwitch %1366 %2743 5 %2747 7 %2765
       %2765 = OpLabel
       %2767 = OpVectorShuffle %v2float %2647 %2647 0 1
       %2768 = OpExtInst %uint %1 PackHalf2x16 %2767
       %2770 = OpVectorShuffle %v2float %2647 %2647 2 3
       %2771 = OpExtInst %uint %1 PackHalf2x16 %2770
       %2772 = OpCompositeConstruct %v2uint %2768 %2771
               OpBranch %2773
       %2747 = OpLabel
       %2749 = OpCompositeExtract %float %2647 0
       %2783 = OpExtInst %float %1 FMax %2749 %float_n1
       %2784 = OpExtInst %float %1 FMin %2783 %float_1
       %2786 = OpFOrdGreaterThanEqual %bool %2784 %float_0
       %2787 = OpSelect %float %2786 %float_0_5 %float_n0_5
       %2791 = OpExtInst %float %1 Fma %2784 %float_32767 %2787
       %2792 = OpConvertFToS %int %2791
       %2793 = OpBitcast %uint %2792
       %2794 = OpBitwiseAnd %uint %2793 %uint_65535
       %2752 = OpCompositeExtract %float %2647 1
       %2800 = OpExtInst %float %1 FMax %2752 %float_n1
       %2801 = OpExtInst %float %1 FMin %2800 %float_1
       %2803 = OpFOrdGreaterThanEqual %bool %2801 %float_0
       %2804 = OpSelect %float %2803 %float_0_5 %float_n0_5
       %2808 = OpExtInst %float %1 Fma %2801 %float_32767 %2804
       %2809 = OpConvertFToS %int %2808
       %2810 = OpBitcast %uint %2809
       %2811 = OpBitwiseAnd %uint %2810 %uint_65535
       %2754 = OpShiftLeftLogical %uint %2811 %uint_16
       %2755 = OpBitwiseOr %uint %2794 %2754
       %2757 = OpCompositeExtract %float %2647 2
       %2817 = OpExtInst %float %1 FMax %2757 %float_n1
       %2818 = OpExtInst %float %1 FMin %2817 %float_1
       %2820 = OpFOrdGreaterThanEqual %bool %2818 %float_0
       %2821 = OpSelect %float %2820 %float_0_5 %float_n0_5
       %2825 = OpExtInst %float %1 Fma %2818 %float_32767 %2821
       %2826 = OpConvertFToS %int %2825
       %2827 = OpBitcast %uint %2826
       %2828 = OpBitwiseAnd %uint %2827 %uint_65535
       %2760 = OpCompositeExtract %float %2647 3
       %2834 = OpExtInst %float %1 FMax %2760 %float_n1
       %2835 = OpExtInst %float %1 FMin %2834 %float_1
       %2837 = OpFOrdGreaterThanEqual %bool %2835 %float_0
       %2838 = OpSelect %float %2837 %float_0_5 %float_n0_5
       %2842 = OpExtInst %float %1 Fma %2835 %float_32767 %2838
       %2843 = OpConvertFToS %int %2842
       %2844 = OpBitcast %uint %2843
       %2845 = OpBitwiseAnd %uint %2844 %uint_65535
       %2762 = OpShiftLeftLogical %uint %2845 %uint_16
       %2763 = OpBitwiseOr %uint %2828 %2762
       %2764 = OpCompositeConstruct %v2uint %2755 %2763
               OpBranch %2773
       %2743 = OpLabel
       %2745 = OpVectorShuffle %v2float %2647 %2647 0 1
       %2746 = OpBitcast %v2uint %2745
               OpBranch %2773
       %2773 = OpLabel
       %3469 = OpPhi %v2uint %2746 %2743 %2764 %2747 %2772 %2765
       %1282 = OpCompositeExtract %uint %3454 0
       %1284 = OpCompositeExtract %uint %3454 1
       %1286 = OpCompositeExtract %uint %3459 0
       %1288 = OpCompositeExtract %uint %3459 1
       %1289 = OpCompositeConstruct %v4uint %1282 %1284 %1286 %1288
       %1291 = OpCompositeExtract %uint %3464 0
       %1293 = OpCompositeExtract %uint %3464 1
       %1295 = OpCompositeExtract %uint %3469 0
       %1297 = OpCompositeExtract %uint %3469 1
       %1298 = OpCompositeConstruct %v4uint %1291 %1293 %1295 %1297
       %2849 = OpIEqual %bool %1673 %uint_0
       %3519 = OpSelect %bool %2849 %false %2849
               OpSelectionMerge %2885 DontFlatten
               OpBranchConditional %3519 %2856 %2885
       %2856 = OpLabel
       %3441 = OpCompositeInsert %v4uint %1286 %1289 0
       %3443 = OpCompositeInsert %v4uint %1288 %3441 1
               OpBranch %2885
       %2885 = OpLabel
       %3475 = OpPhi %v4uint %1289 %2773 %3443 %2856
               OpSelectionMerge %2918 DontFlatten
               OpBranchConditional %1427 %2889 %2918
       %2889 = OpLabel
       %2892 = OpIEqual %bool %1366 %uint_5
       %2893 = OpLogicalNot %bool %2892
               OpSelectionMerge %2898 None
               OpBranchConditional %2893 %2894 %2898
       %2894 = OpLabel
       %2897 = OpIEqual %bool %1366 %uint_7
               OpBranch %2898
       %2898 = OpLabel
       %2899 = OpPhi %bool %2892 %2889 %2897 %2894
               OpSelectionMerge %2917 DontFlatten
               OpBranchConditional %2899 %2900 %2917
       %2900 = OpLabel
       %2903 = OpBitwiseAnd %v4uint %3475 %3505
       %2905 = OpVectorShuffle %v4uint %3475 %3475 1 0 3 2
       %2907 = OpBitwiseAnd %v4uint %2905 %3506
       %2908 = OpBitwiseOr %v4uint %2903 %2907
       %2911 = OpBitwiseAnd %v4uint %1298 %3505
       %2913 = OpVectorShuffle %v4uint %1298 %1298 1 0 3 2
       %2915 = OpBitwiseAnd %v4uint %2913 %3506
       %2916 = OpBitwiseOr %v4uint %2911 %2915
               OpBranch %2917
       %2917 = OpLabel
       %3483 = OpPhi %v4uint %1298 %2898 %2916 %2900
       %3481 = OpPhi %v4uint %3475 %2898 %2908 %2900
               OpBranch %2918
       %2918 = OpLabel
       %3482 = OpPhi %v4uint %1298 %2885 %3483 %2917
       %3480 = OpPhi %v4uint %3475 %2885 %3481 %2917
       %2925 = OpIAdd %v2uint %3445 %1446
               OpSelectionMerge %2945 DontFlatten
               OpBranchConditional %1406 %2928 %2939
       %2939 = OpLabel
       %2941 = OpBitcast %v2int %2925
       %3030 = OpCompositeExtract %int %2941 1
       %3031 = OpShiftRightArithmetic %int %3030 %int_5
       %3032 = OpBitcast %int %1430
       %3033 = OpIMul %int %3031 %3032
       %3034 = OpCompositeExtract %int %2941 0
       %3035 = OpShiftRightArithmetic %int %3034 %int_5
       %3036 = OpIAdd %int %3033 %3035
       %3037 = OpShiftLeftLogical %int %3036 %int_6
       %3039 = OpShiftRightArithmetic %int %3030 %int_1
       %3040 = OpBitwiseAnd %int %3039 %int_7
       %3041 = OpShiftLeftLogical %int %3040 %int_3
       %3043 = OpBitwiseAnd %int %3034 %int_7
       %3044 = OpBitwiseOr %int %3041 %3043
       %3047 = OpBitwiseOr %int %3037 %3044
       %3048 = OpShiftLeftLogical %int %3047 %uint_3
       %3050 = OpShiftRightArithmetic %int %3030 %int_4
       %3051 = OpBitwiseAnd %int %3050 %int_1
       %3053 = OpShiftRightArithmetic %int %3034 %int_3
       %3054 = OpBitwiseAnd %int %3053 %int_3
       %3056 = OpShiftRightArithmetic %int %3030 %int_3
       %3057 = OpBitwiseAnd %int %3056 %int_1
       %3058 = OpShiftLeftLogical %int %3057 %int_1
       %3059 = OpBitwiseXor %int %3054 %3058
       %3064 = OpBitwiseAnd %int %3030 %int_1
       %3068 = OpShiftLeftLogical %int %3064 %int_4
       %3069 = OpShiftLeftLogical %int %3059 %int_6
       %3070 = OpBitwiseOr %int %3068 %3069
       %3071 = OpShiftLeftLogical %int %3051 %int_11
       %3072 = OpBitwiseOr %int %3070 %3071
       %3073 = OpBitwiseAnd %int %3048 %int_15
       %3074 = OpBitwiseOr %int %3072 %3073
       %3075 = OpShiftRightArithmetic %int %3048 %int_4
       %3076 = OpBitwiseAnd %int %3075 %int_1
       %3077 = OpShiftLeftLogical %int %3076 %int_5
       %3078 = OpBitwiseOr %int %3074 %3077
       %3079 = OpShiftRightArithmetic %int %3048 %int_5
       %3080 = OpBitwiseAnd %int %3079 %int_7
       %3081 = OpShiftLeftLogical %int %3080 %int_8
       %3082 = OpBitwiseOr %int %3078 %3081
       %3083 = OpShiftRightArithmetic %int %3048 %int_8
       %3084 = OpShiftLeftLogical %int %3083 %int_12
       %3085 = OpBitwiseOr %int %3082 %3084
       %2944 = OpBitcast %uint %3085
               OpBranch %2945
       %2928 = OpLabel
       %2931 = OpCompositeExtract %uint %2925 0
       %2932 = OpCompositeExtract %uint %2925 1
       %2933 = OpCompositeConstruct %v3uint %2931 %2932 %1410
       %2934 = OpBitcast %v3int %2933
       %2957 = OpCompositeExtract %int %2934 2
       %2958 = OpShiftRightArithmetic %int %2957 %int_2
       %2959 = OpBitcast %int %1435
       %2960 = OpIMul %int %2958 %2959
       %2961 = OpCompositeExtract %int %2934 1
       %2962 = OpShiftRightArithmetic %int %2961 %int_4
       %2963 = OpIAdd %int %2960 %2962
       %2964 = OpBitcast %int %1430
       %2965 = OpIMul %int %2963 %2964
       %2966 = OpCompositeExtract %int %2934 0
       %2967 = OpShiftRightArithmetic %int %2966 %int_5
       %2968 = OpIAdd %int %2965 %2967
       %2969 = OpShiftLeftLogical %int %2968 %int_7
       %2971 = OpBitwiseAnd %int %2957 %int_3
       %2972 = OpShiftLeftLogical %int %2971 %int_5
       %2974 = OpShiftRightArithmetic %int %2961 %int_1
       %2975 = OpBitwiseAnd %int %2974 %int_3
       %2976 = OpShiftLeftLogical %int %2975 %int_3
       %2977 = OpBitwiseOr %int %2972 %2976
       %2979 = OpBitwiseAnd %int %2966 %int_7
       %2980 = OpBitwiseOr %int %2977 %2979
       %2983 = OpBitwiseOr %int %2969 %2980
       %2984 = OpShiftLeftLogical %int %2983 %uint_3
       %2986 = OpShiftRightArithmetic %int %2961 %int_3
       %2989 = OpBitwiseXor %int %2986 %2958
       %2990 = OpBitwiseAnd %int %2989 %int_1
       %2992 = OpShiftRightArithmetic %int %2966 %int_3
       %2993 = OpBitwiseAnd %int %2992 %int_3
       %2995 = OpShiftLeftLogical %int %2990 %int_1
       %2996 = OpBitwiseXor %int %2993 %2995
       %3001 = OpBitwiseAnd %int %2961 %int_1
       %3005 = OpShiftLeftLogical %int %3001 %int_4
       %3006 = OpShiftLeftLogical %int %2996 %int_6
       %3007 = OpBitwiseOr %int %3005 %3006
       %3008 = OpShiftLeftLogical %int %2990 %int_11
       %3009 = OpBitwiseOr %int %3007 %3008
       %3010 = OpBitwiseAnd %int %2984 %int_15
       %3011 = OpBitwiseOr %int %3009 %3010
       %3012 = OpShiftRightArithmetic %int %2984 %int_4
       %3013 = OpBitwiseAnd %int %3012 %int_1
       %3014 = OpShiftLeftLogical %int %3013 %int_5
       %3015 = OpBitwiseOr %int %3011 %3014
       %3016 = OpShiftRightArithmetic %int %2984 %int_5
       %3017 = OpBitwiseAnd %int %3016 %int_7
       %3018 = OpShiftLeftLogical %int %3017 %int_8
       %3019 = OpBitwiseOr %int %3015 %3018
       %3020 = OpShiftRightArithmetic %int %2984 %int_8
       %3021 = OpShiftLeftLogical %int %3020 %int_12
       %3022 = OpBitwiseOr %int %3019 %3021
       %2938 = OpBitcast %uint %3022
               OpBranch %2945
       %2945 = OpLabel
       %3484 = OpPhi %uint %2938 %2928 %2944 %2939
       %2948 = OpIAdd %uint %3484 %1453
       %1316 = OpShiftRightLogical %uint %2948 %int_4
       %3091 = OpIEqual %bool %1402 %uint_4
               OpSelectionMerge %3095 None
               OpBranchConditional %3091 %3092 %3095
       %3092 = OpLabel
       %3094 = OpVectorShuffle %v4uint %3480 %3480 1 0 3 2
               OpBranch %3095
       %3095 = OpLabel
       %3486 = OpPhi %v4uint %3480 %2945 %3094 %3092
       %3520 = OpSelect %uint %3091 %uint_2 %1402
       %3102 = OpIEqual %bool %3520 %uint_1
       %3104 = OpIEqual %bool %3520 %uint_2
       %3105 = OpLogicalOr %bool %3102 %3104
               OpSelectionMerge %3118 None
               OpBranchConditional %3105 %3106 %3118
       %3106 = OpLabel
       %3109 = OpBitwiseAnd %v4uint %3486 %3507
       %3111 = OpShiftLeftLogical %v4uint %3109 %3508
       %3114 = OpBitwiseAnd %v4uint %3486 %3509
       %3116 = OpShiftRightLogical %v4uint %3114 %3508
       %3117 = OpBitwiseOr %v4uint %3111 %3116
               OpBranch %3118
       %3118 = OpLabel
       %3488 = OpPhi %v4uint %3486 %3095 %3117 %3106
       %3122 = OpIEqual %bool %3520 %uint_3
       %3123 = OpLogicalOr %bool %3104 %3122
               OpSelectionMerge %3132 None
               OpBranchConditional %3123 %3124 %3132
       %3124 = OpLabel
       %3127 = OpShiftLeftLogical %v4uint %3488 %3510
       %3130 = OpShiftRightLogical %v4uint %3488 %3510
       %3131 = OpBitwiseOr %v4uint %3127 %3130
               OpBranch %3132
       %3132 = OpLabel
       %3489 = OpPhi %v4uint %3488 %3118 %3131 %3124
       %1321 = OpAccessChain %_ptr_StorageBuffer_v4uint %xe_resolve_dest %int_0 %1316
               OpStore %1321 %3489
       %1324 = OpIAdd %uint %2948 %uint_32
       %1326 = OpShiftRightLogical %uint %1324 %int_4
               OpSelectionMerge %3175 None
               OpBranchConditional %3091 %3172 %3175
       %3172 = OpLabel
       %3174 = OpVectorShuffle %v4uint %3482 %3482 1 0 3 2
               OpBranch %3175
       %3175 = OpLabel
       %3497 = OpPhi %v4uint %3482 %3132 %3174 %3172
               OpSelectionMerge %3198 None
               OpBranchConditional %3105 %3186 %3198
       %3186 = OpLabel
       %3189 = OpBitwiseAnd %v4uint %3497 %3507
       %3191 = OpShiftLeftLogical %v4uint %3189 %3508
       %3194 = OpBitwiseAnd %v4uint %3497 %3509
       %3196 = OpShiftRightLogical %v4uint %3194 %3508
       %3197 = OpBitwiseOr %v4uint %3191 %3196
               OpBranch %3198
       %3198 = OpLabel
       %3499 = OpPhi %v4uint %3497 %3175 %3197 %3186
               OpSelectionMerge %3212 None
               OpBranchConditional %3123 %3204 %3212
       %3204 = OpLabel
       %3207 = OpShiftLeftLogical %v4uint %3499 %3510
       %3210 = OpShiftRightLogical %v4uint %3499 %3510
       %3211 = OpBitwiseOr %v4uint %3207 %3210
               OpBranch %3212
       %3212 = OpLabel
       %3500 = OpPhi %v4uint %3499 %3198 %3211 %3204
       %1331 = OpAccessChain %_ptr_StorageBuffer_v4uint %xe_resolve_dest %int_0 %1326
               OpStore %1331 %3500
               OpBranch %1332
       %1332 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t resolve_host_color_64bpp_4xmsaa_cs[] = {
    0x07230203, 0x00010300, 0x000D000A, 0x00000DC1, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x00000004, 0x6E69616D, 0x00000000, 0x0000048D, 0x00060010, 0x00000004,
    0x00000011, 0x00000008, 0x00000008, 0x00000001, 0x00030003, 0x00000002,
    0x000001CC, 0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F,
    0x5F776F6C, 0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47,
    0x735F5458, 0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572,
    0x74636E75, 0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6E69616D,
    0x00000000, 0x00070005, 0x0000015F, 0x68737570, 0x6E6F635F, 0x625F7473,
    0x6B636F6C, 0x0065785F, 0x00090006, 0x0000015F, 0x00000000, 0x725F6578,
    0x6C6F7365, 0x655F6576, 0x6D617264, 0x666E695F, 0x0000006F, 0x000A0006,
    0x0000015F, 0x00000001, 0x725F6578, 0x6C6F7365, 0x635F6576, 0x64726F6F,
    0x74616E69, 0x6E695F65, 0x00006F66, 0x00090006, 0x0000015F, 0x00000002,
    0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365, 0x6F666E69, 0x00000000,
    0x000B0006, 0x0000015F, 0x00000003, 0x725F6578, 0x6C6F7365, 0x645F6576,
    0x5F747365, 0x726F6F63, 0x616E6964, 0x695F6574, 0x006F666E, 0x00090006,
    0x0000015F, 0x00000004, 0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365,
    0x65736162, 0x00000000, 0x00060005, 0x00000161, 0x68737570, 0x6E6F635F,
    0x5F737473, 0x00006578, 0x000A0005, 0x000002CB, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x65785F72, 0x6F6C625F, 0x00006B63,
    0x000D0006, 0x000002CB, 0x00000000, 0x725F6578, 0x6C6F7365, 0x685F6576,
    0x5F74736F, 0x6F6C6F63, 0x69645F72, 0x74617073, 0x6F5F6863, 0x65736666,
    0x00000074, 0x000B0006, 0x000002CB, 0x00000001, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x75645F72, 0x625F706D, 0x00657361,
    0x000D0006, 0x000002CB, 0x00000002, 0x725F6578, 0x6C6F7365, 0x685F6576,
    0x5F74736F, 0x6F6C6F63, 0x75645F72, 0x705F706D, 0x68637469, 0x6C69745F,
    0x00007365, 0x000D0006, 0x000002CB, 0x00000003, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x6F735F72, 0x65637275, 0x7361625F,
    0x69745F65, 0x0073656C, 0x000E0006, 0x000002CB, 0x00000004, 0x725F6578,
    0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x6F735F72, 0x65637275,
    0x7469705F, 0x745F6863, 0x73656C69, 0x00000000, 0x000D0006, 0x000002CB,
    0x00000005, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63,
    0x68745F72, 0x64616572, 0x756F635F, 0x785F746E, 0x00000000, 0x000D0006,
    0x000002CB, 0x00000006, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F,
    0x6F6C6F63, 0x68745F72, 0x64616572, 0x756F635F, 0x795F746E, 0x00000000,
    0x000C0006, 0x000002CB, 0x00000007, 0x725F6578, 0x6C6F7365, 0x685F6576,
    0x5F74736F, 0x6F6C6F63, 0x65685F72, 0x74686769, 0x6163735F, 0x0064656C,
    0x000D0006, 0x000002CB, 0x00000008, 0x725F6578, 0x6C6F7365, 0x685F6576,
    0x5F74736F, 0x6F6C6F63, 0x736D5F72, 0x325F6161, 0x61735F78, 0x656C706D,
    0x0000305F, 0x000D0006, 0x000002CB, 0x00000009, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x736D5F72, 0x325F6161, 0x61735F78,
    0x656C706D, 0x0000315F, 0x000A0006, 0x000002CB, 0x0000000A, 0x725F6578,
    0x6C6F7365, 0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x6C665F72, 0x00736761,
    0x00080005, 0x000002CD, 0x725F6578, 0x6C6F7365, 0x685F6576, 0x5F74736F,
    0x6F6C6F63, 0x00000072, 0x000A0005, 0x00000312, 0x725F6578, 0x6C6F7365,
    0x685F6576, 0x5F74736F, 0x6F6C6F63, 0x6F735F72, 0x65637275, 0x00000000,
    0x00090005, 0x00000473, 0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365,
    0x625F6578, 0x6B636F6C, 0x00000000, 0x00050006, 0x00000473, 0x00000000,
    0x61746164, 0x00000000, 0x00060005, 0x00000475, 0x725F6578, 0x6C6F7365,
    0x645F6576, 0x00747365, 0x00080005, 0x0000048D, 0x475F6C67, 0x61626F6C,
    0x766E496C, 0x7461636F, 0x496E6F69, 0x00000044, 0x00050048, 0x0000015F,
    0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x0000015F, 0x00000001,
    0x00000023, 0x00000004, 0x00050048, 0x0000015F, 0x00000002, 0x00000023,
    0x00000008, 0x00050048, 0x0000015F, 0x00000003, 0x00000023, 0x0000000C,
    0x00050048, 0x0000015F, 0x00000004, 0x00000023, 0x00000010, 0x00030047,
    0x0000015F, 0x00000002, 0x00050048, 0x000002CB, 0x00000000, 0x00000023,
    0x00000000, 0x00050048, 0x000002CB, 0x00000001, 0x00000023, 0x00000004,
    0x00050048, 0x000002CB, 0x00000002, 0x00000023, 0x00000008, 0x00050048,
    0x000002CB, 0x00000003, 0x00000023, 0x0000000C, 0x00050048, 0x000002CB,
    0x00000004, 0x00000023, 0x00000010, 0x00050048, 0x000002CB, 0x00000005,
    0x00000023, 0x00000014, 0x00050048, 0x000002CB, 0x00000006, 0x00000023,
    0x00000018, 0x00050048, 0x000002CB, 0x00000007, 0x00000023, 0x0000001C,
    0x00050048, 0x000002CB, 0x00000008, 0x00000023, 0x00000020, 0x00050048,
    0x000002CB, 0x00000009, 0x00000023, 0x00000024, 0x00050048, 0x000002CB,
    0x0000000A, 0x00000023, 0x00000028, 0x00030047, 0x000002CB, 0x00000002,
    0x00040047, 0x000002CD, 0x00000022, 0x00000000, 0x00040047, 0x000002CD,
    0x00000021, 0x00000001, 0x00040047, 0x00000312, 0x00000022, 0x00000002,
    0x00040047, 0x00000312, 0x00000021, 0x00000000, 0x00040047, 0x00000472,
    0x00000006, 0x00000010, 0x00040048, 0x00000473, 0x00000000, 0x00000019,
    0x00050048, 0x00000473, 0x00000000, 0x00000023, 0x00000000, 0x00030047,
    0x00000473, 0x00000002, 0x00040047, 0x00000475, 0x00000022, 0x00000001,
    0x00040047, 0x00000475, 0x00000021, 0x00000000, 0x00040047, 0x0000048D,
    0x0000000B, 0x0000001C, 0x00040047, 0x00000492, 0x0000000B, 0x00000019,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015,
    0x00000006, 0x00000020, 0x00000000, 0x00040017, 0x00000008, 0x00000006,
    0x00000002, 0x00040017, 0x0000000D, 0x00000006, 0x00000004, 0x00040015,
    0x00000018, 0x00000020, 0x00000001, 0x00040017, 0x00000020, 0x00000018,
    0x00000002, 0x00040017, 0x00000027, 0x00000018, 0x00000003, 0x00020014,
    0x00000034, 0x00030016, 0x00000035, 0x00000020, 0x00040017, 0x00000066,
    0x00000035, 0x00000004, 0x0004002B, 0x00000006, 0x0000009E, 0x00000001,
    0x0004002B, 0x00000006, 0x000000A1, 0x00000002, 0x0004002B, 0x00000006,
    0x000000A7, 0x00FF00FF, 0x0004002B, 0x00000006, 0x000000AA, 0x00000008,
    0x0004002B, 0x00000006, 0x000000AE, 0xFF00FF00, 0x0004002B, 0x00000006,
    0x000000B7, 0x00000003, 0x0004002B, 0x00000006, 0x000000BD, 0x00000010,
    0x0004002B, 0x00000006, 0x000000C8, 0x00000004, 0x0004002B, 0x00000018,
    0x000000D5, 0x00000004, 0x0004002B, 0x00000018, 0x000000D7, 0x00000006,
    0x0004002B, 0x00000018, 0x000000DA, 0x0000000B, 0x0004002B, 0x00000018,
    0x000000DD, 0x0000000F, 0x0004002B, 0x00000018, 0x000000E1, 0x00000001,
    0x0004002B, 0x00000018, 0x000000E3, 0x00000005, 0x0004002B, 0x00000018,
    0x000000E7, 0x00000007, 0x0004002B, 0x00000018, 0x000000E9, 0x00000008,
    0x0004002B, 0x00000018, 0x000000ED, 0x0000000C, 0x0004002B, 0x00000006,
    0x000000F8, 0x00000000, 0x0004002B, 0x00000018, 0x00000101, 0x00000003,
    0x0004002B, 0x00000018, 0x00000122, 0x00000002, 0x0004002B, 0x00000018,
    0x00000158, 0x00000000, 0x0007001E, 0x0000015F, 0x00000006, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00040020, 0x00000160, 0x00000009,
    0x0000015F, 0x0004003B, 0x00000160, 0x00000161, 0x00000009, 0x00040020,
    0x00000162, 0x00000009, 0x00000006, 0x0004002B, 0x00000006, 0x0000016A,
    0x000003FF, 0x0004002B, 0x00000006, 0x0000016E, 0x0000000A, 0x0004002B,
    0x00000006, 0x0000017B, 0x000007FF, 0x0004002B, 0x00000006, 0x0000017F,
    0x00000018, 0x0004002B, 0x00000006, 0x00000181, 0x0000000F, 0x0004002B,
    0x00000006, 0x00000185, 0x0000001C, 0x0005002C, 0x00000008, 0x00000192,
    0x000000F8, 0x000000C8, 0x0005002C, 0x00000008, 0x00000196, 0x000000C8,
    0x0000009E, 0x0004002B, 0x00000006, 0x000001A3, 0x00000005, 0x0004002B,
    0x00000006, 0x000001B2, 0x00000007, 0x0004002B, 0x00000035, 0x000001CB,
    0x3F800000, 0x0004002B, 0x00000006, 0x000001D1, 0x01000000, 0x0004002B,
    0x00000006, 0x000001E3, 0x00000014, 0x0005002C, 0x00000008, 0x000001E4,
    0x000001E3, 0x0000017F, 0x00040017, 0x00000205, 0x00000006, 0x00000003,
    0x0004002B, 0x00000006, 0x0000023F, 0xFFFF0000, 0x0004002B, 0x00000006,
    0x00000244, 0x0000FFFF, 0x0004002B, 0x00000006, 0x00000250, 0x00000028,
    0x0004002B, 0x00000035, 0x0000026B, 0xBF800000, 0x0004002B, 0x00000035,
    0x00000270, 0x00000000, 0x0004002B, 0x00000035, 0x00000272, 0x3F000000,
    0x0004002B, 0x00000035, 0x00000273, 0xBF000000, 0x0004002B, 0x00000035,
    0x00000276, 0x46FFFE00, 0x00040017, 0x0000029A, 0x00000035, 0x00000002,
    0x000D001E, 0x000002CB, 0x00000006, 0x00000006, 0x00000006, 0x00000006,
    0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006,
    0x00000006, 0x00040020, 0x000002CC, 0x00000002, 0x000002CB, 0x0004003B,
    0x000002CC, 0x000002CD, 0x00000002, 0x00040020, 0x000002CE, 0x00000002,
    0x00000006, 0x0005002C, 0x00000008, 0x000002FB, 0x0000009E, 0x000000F8,
    0x00090019, 0x00000310, 0x00000035, 0x00000001, 0x00000000, 0x00000000,
    0x00000001, 0x00000001, 0x00000000, 0x00040020, 0x00000311, 0x00000000,
    0x00000310, 0x0004003B, 0x00000311, 0x00000312, 0x00000000, 0x0003002A,
    0x00000034, 0x00000335, 0x00030029, 0x00000034, 0x000003A7, 0x0003001D,
    0x00000472, 0x0000000D, 0x0003001E, 0x00000473, 0x00000472, 0x00040020,
    0x00000474, 0x0000000C, 0x00000473, 0x0004003B, 0x00000474, 0x00000475,
    0x0000000C, 0x00040020, 0x0000047E, 0x0000000C, 0x0000000D, 0x00040020,
    0x0000048C, 0x00000001, 0x00000205, 0x0004003B, 0x0000048C, 0x0000048D,
    0x00000001, 0x0006002C, 0x00000205, 0x00000492, 0x000000AA, 0x000000AA,
    0x0000009E, 0x00030001, 0x00000008, 0x00000D77, 0x0005002C, 0x00000008,
    0x00000DAD, 0x0000009E, 0x0000009E, 0x0005002C, 0x00000008, 0x00000DAF,
    0x000000B7, 0x000000B7, 0x0005002C, 0x00000008, 0x00000DB0, 0x00000181,
    0x00000181, 0x0007002C, 0x0000000D, 0x00000DB1, 0x0000023F, 0x0000023F,
    0x0000023F, 0x0000023F, 0x0007002C, 0x0000000D, 0x00000DB2, 0x00000244,
    0x00000244, 0x00000244, 0x00000244, 0x0007002C, 0x0000000D, 0x00000DB3,
    0x000000A7, 0x000000A7, 0x000000A7, 0x000000A7, 0x0007002C, 0x0000000D,
    0x00000DB4, 0x000000AA, 0x000000AA, 0x000000AA, 0x000000AA, 0x0007002C,
    0x0000000D, 0x00000DB5, 0x000000AE, 0x000000AE, 0x000000AE, 0x000000AE,
    0x0007002C, 0x0000000D, 0x00000DB6, 0x000000BD, 0x000000BD, 0x000000BD,
    0x000000BD, 0x0004002B, 0x00000006, 0x00000DBA, 0x00000020, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200F8, 0x00000005,
    0x0004003D, 0x00000205, 0x0000048F, 0x0000048D, 0x000300F7, 0x00000534,
    0x00000000, 0x000300FB, 0x000000F8, 0x000004D8, 0x000200F8, 0x000004D8,
    0x00050041, 0x00000162, 0x00000541, 0x00000161, 0x00000158, 0x0004003D,
    0x00000006, 0x00000542, 0x00000541, 0x00050041, 0x00000162, 0x00000543,
    0x00000161, 0x000000E1, 0x0004003D, 0x00000006, 0x00000544, 0x00000543,
    0x000500C2, 0x00000006, 0x00000555, 0x00000542, 0x0000017F, 0x000500C7,
    0x00000006, 0x00000556, 0x00000555, 0x00000181, 0x00050050, 0x00000008,
    0x000005BE, 0x00000544, 0x00000544, 0x000500C2, 0x00000008, 0x00000562,
    0x000005BE, 0x00000192, 0x000500C4, 0x00000008, 0x00000564, 0x00000DAD,
    0x00000196, 0x00050082, 0x00000008, 0x00000566, 0x00000564, 0x00000DAD,
    0x000500C7, 0x00000008, 0x00000567, 0x00000562, 0x00000566, 0x000500C4,
    0x00000008, 0x00000569, 0x00000567, 0x00000DAF, 0x00050084, 0x00000008,
    0x0000056C, 0x00000569, 0x00000DAD, 0x000500C2, 0x00000006, 0x0000056F,
    0x00000544, 0x000001A3, 0x000500C7, 0x00000006, 0x00000570, 0x0000056F,
    0x0000017B, 0x00050041, 0x00000162, 0x00000575, 0x00000161, 0x00000122,
    0x0004003D, 0x00000006, 0x00000576, 0x00000575, 0x00050041, 0x00000162,
    0x00000577, 0x00000161, 0x00000101, 0x0004003D, 0x00000006, 0x00000578,
    0x00000577, 0x000500C7, 0x00000006, 0x0000057A, 0x00000576, 0x000001B2,
    0x000500C7, 0x00000006, 0x0000057D, 0x00000576, 0x000000AA, 0x000500AB,
    0x00000034, 0x0000057E, 0x0000057D, 0x000000F8, 0x000500C2, 0x00000006,
    0x00000581, 0x00000576, 0x000000C8, 0x000500C7, 0x00000006, 0x00000582,
    0x00000581, 0x000001B2, 0x000500C7, 0x00000006, 0x00000592, 0x00000576,
    0x000001D1, 0x000500AB, 0x00000034, 0x00000593, 0x00000592, 0x000000F8,
    0x000500C7, 0x00000006, 0x00000596, 0x00000578, 0x0000016A, 0x000500C2,
    0x00000006, 0x00000599, 0x00000578, 0x0000016E, 0x000500C7, 0x00000006,
    0x0000059A, 0x00000599, 0x0000016A, 0x000500C4, 0x00000006, 0x0000059B,
    0x0000059A, 0x000000E1, 0x00050050, 0x00000008, 0x000005C8, 0x00000578,
    0x00000578, 0x000500C2, 0x00000008, 0x0000059F, 0x000005C8, 0x000001E4,
    0x000500C7, 0x00000008, 0x000005A1, 0x0000059F, 0x00000DB0, 0x000500C4,
    0x00000008, 0x000005A3, 0x000005A1, 0x00000DAF, 0x00050084, 0x00000008,
    0x000005A6, 0x000005A3, 0x00000DAD, 0x000500C2, 0x00000006, 0x000005A9,
    0x00000578, 0x00000185, 0x000500C7, 0x00000006, 0x000005AA, 0x000005A9,
    0x000001B2, 0x00050041, 0x00000162, 0x000005AC, 0x00000161, 0x000000D5,
    0x0004003D, 0x00000006, 0x000005AD, 0x000005AC, 0x000300F7, 0x0000064C,
    0x00000000, 0x000300FB, 0x000000F8, 0x000005DD, 0x000200F8, 0x000005DD,
    0x00050051, 0x00000006, 0x000005DF, 0x0000048F, 0x00000000, 0x00050041,
    0x000002CE, 0x000005E0, 0x000002CD, 0x000000E3, 0x0004003D, 0x00000006,
    0x000005E1, 0x000005E0, 0x000500AE, 0x00000034, 0x000005E2, 0x000005DF,
    0x000005E1, 0x000400A8, 0x00000034, 0x000005E3, 0x000005E2, 0x000300F7,
    0x000005EA, 0x00000000, 0x000400FA, 0x000005E3, 0x000005E4, 0x000005EA,
    0x000200F8, 0x000005E4, 0x00050051, 0x00000006, 0x000005E6, 0x0000048F,
    0x00000001, 0x00050041, 0x000002CE, 0x000005E7, 0x000002CD, 0x000000D7,
    0x0004003D, 0x00000006, 0x000005E8, 0x000005E7, 0x000500AE, 0x00000034,
    0x000005E9, 0x000005E6, 0x000005E8, 0x000200F9, 0x000005EA, 0x000200F8,
    0x000005EA, 0x000700F5, 0x00000034, 0x000005EB, 0x000005E2, 0x000005DD,
    0x000005E9, 0x000005E4, 0x000300F7, 0x000005ED, 0x00000000, 0x000400FA,
    0x000005EB, 0x000005EC, 0x000005ED, 0x000200F8, 0x000005EC, 0x000200F9,
    0x0000064C, 0x000200F8, 0x000005ED, 0x00050084, 0x00000006, 0x000005F6,
    0x000005DF, 0x000000C8, 0x00050051, 0x00000006, 0x000005F8, 0x0000048F,
    0x00000001, 0x00050086, 0x00000006, 0x000005FB, 0x000005F6, 0x000001E3,
    0x00050086, 0x00000006, 0x000005FE, 0x000005F8, 0x000000AA, 0x00050084,
    0x00000006, 0x00000602, 0x000005FB, 0x000001E3, 0x00050082, 0x00000006,
    0x00000603, 0x000005F6, 0x00000602, 0x00050084, 0x00000006, 0x00000607,
    0x000005FE, 0x000000AA, 0x00050082, 0x00000006, 0x00000608, 0x000005F8,
    0x00000607, 0x00050041, 0x000002CE, 0x00000609, 0x000002CD, 0x00000158,
    0x0004003D, 0x00000006, 0x0000060A, 0x00000609, 0x00050041, 0x000002CE,
    0x0000060C, 0x000002CD, 0x00000122, 0x0004003D, 0x00000006, 0x0000060D,
    0x0000060C, 0x00050084, 0x00000006, 0x0000060E, 0x000005FE, 0x0000060D,
    0x00050080, 0x00000006, 0x0000060F, 0x0000060A, 0x0000060E, 0x00050080,
    0x00000006, 0x00000611, 0x0000060F, 0x000005FB, 0x00050086, 0x00000006,
    0x00000616, 0x00000611, 0x0000060D, 0x00050084, 0x00000006, 0x0000061A,
    0x00000616, 0x0000060D, 0x00050082, 0x00000006, 0x0000061B, 0x00000611,
    0x0000061A, 0x00050084, 0x00000006, 0x0000061E, 0x0000061B, 0x000001E3,
    0x00050080, 0x00000006, 0x00000620, 0x0000061E, 0x00000603, 0x00050084,
    0x00000006, 0x00000623, 0x00000616, 0x000000AA, 0x00050080, 0x00000006,
    0x00000625, 0x00000623, 0x00000608, 0x00050050, 0x00000008, 0x00000626,
    0x00000620, 0x00000625, 0x00050051, 0x00000006, 0x0000062A, 0x0000056C,
    0x00000000, 0x000500B0, 0x00000034, 0x0000062B, 0x00000620, 0x0000062A,
    0x000400A8, 0x00000034, 0x0000062C, 0x0000062B, 0x000300F7, 0x00000633,
    0x00000000, 0x000400FA, 0x0000062C, 0x0000062D, 0x00000633, 0x000200F8,
    0x0000062D, 0x00050051, 0x00000006, 0x00000631, 0x0000056C, 0x00000001,
    0x000500B0, 0x00000034, 0x00000632, 0x00000625, 0x00000631, 0x000200F9,
    0x00000633, 0x000200F8, 0x00000633, 0x000700F5, 0x00000034, 0x00000634,
    0x0000062B, 0x000005ED, 0x00000632, 0x0000062D, 0x000300F7, 0x00000636,
    0x00000000, 0x000400FA, 0x00000634, 0x00000635, 0x00000636, 0x000200F8,
    0x00000635, 0x000200F9, 0x0000064C, 0x000200F8, 0x00000636, 0x00050082,
    0x00000008, 0x0000063A, 0x00000626, 0x0000056C, 0x00050051, 0x00000006,
    0x0000063C, 0x0000063A, 0x00000000, 0x000500C4, 0x00000006, 0x0000063F,
    0x00000570, 0x000000B7, 0x000500AE, 0x00000034, 0x00000640, 0x0000063C,
    0x0000063F, 0x000400A8, 0x00000034, 0x00000641, 0x00000640, 0x000300F7,
    0x00000648, 0x00000000, 0x000400FA, 0x00000641, 0x00000642, 0x00000648,
    0x000200F8, 0x00000642, 0x00050051, 0x00000006, 0x00000644, 0x0000063A,
    0x00000001, 0x00050041, 0x000002CE, 0x00000645, 0x000002CD, 0x000000E7,
    0x0004003D, 0x00000006, 0x00000646, 0x00000645, 0x000500AE, 0x00000034,
    0x00000647, 0x00000644, 0x00000646, 0x000200F9, 0x00000648, 0x000200F8,
    0x00000648, 0x000700F5, 0x00000034, 0x00000649, 0x00000640, 0x00000636,
    0x00000647, 0x00000642, 0x000300F7, 0x0000064B, 0x00000000, 0x000400FA,
    0x00000649, 0x0000064A, 0x0000064B, 0x000200F8, 0x0000064A, 0x000200F9,
    0x0000064C, 0x000200F8, 0x0000064B, 0x000200F9, 0x0000064C, 0x000200F8,
    0x0000064C, 0x000B00F5, 0x00000008, 0x00000D75, 0x00000D77, 0x000005EC,
    0x00000D77, 0x00000635, 0x0000063A, 0x0000064A, 0x0000063A, 0x0000064B,
    0x000B00F5, 0x00000034, 0x00000D74, 0x00000335, 0x000005EC, 0x00000335,
    0x00000635, 0x00000335, 0x0000064A, 0x000003A7, 0x0000064B, 0x000400A8,
    0x00000034, 0x000004DE, 0x00000D74, 0x000300F7, 0x000004E0, 0x00000000,
    0x000400FA, 0x000004DE, 0x000004DF, 0x000004E0, 0x000200F8, 0x000004DF,
    0x000200F9, 0x00000534, 0x000200F8, 0x000004E0, 0x000500B2, 0x00000034,
    0x00000678, 0x000005AA, 0x000000B7, 0x000300F7, 0x00000681, 0x00000000,
    0x000400FA, 0x00000678, 0x00000679, 0x0000067B, 0x000200F8, 0x0000067B,
    0x000500AA, 0x00000034, 0x0000067D, 0x000005AA, 0x000001A3, 0x000600A9,
    0x00000006, 0x00000DBB, 0x0000067D, 0x000000A1, 0x000000F8, 0x000200F9,
    0x00000681, 0x000200F8, 0x00000679, 0x000200F9, 0x00000681, 0x000200F8,
    0x00000681, 0x000700F5, 0x00000006, 0x00000D7A, 0x000005AA, 0x00000679,
    0x00000DBB, 0x0000067B, 0x00050051, 0x00000006, 0x00000689, 0x00000D75,
    0x00000000, 0x00050051, 0x00000006, 0x0000068D, 0x00000D75, 0x00000001,
    0x0007000C, 0x00000006, 0x00000690, 0x00000001, 0x00000029, 0x0000068D,
    0x000000F8, 0x00050050, 0x00000008, 0x00000691, 0x00000689, 0x00000690,
    0x00050080, 0x00000008, 0x00000694, 0x00000691, 0x0000056C, 0x000500C4,
    0x00000008, 0x00000697, 0x00000694, 0x00000DAD, 0x00050050, 0x00000008,
    0x000006AC, 0x00000D7A, 0x00000D7A, 0x000500C2, 0x00000008, 0x000006A5,
    0x000006AC, 0x000002FB, 0x000500C7, 0x00000008, 0x000006A7, 0x000006A5,
    0x00000DAD, 0x00050080, 0x00000008, 0x0000069A, 0x00000697, 0x000006A7,
    0x00050051, 0x00000006, 0x000006E3, 0x0000069A, 0x00000000, 0x00050086,
    0x00000006, 0x000006E5, 0x000006E3, 0x00000250, 0x00050051, 0x00000006,
    0x000006E7, 0x0000069A, 0x00000001, 0x00050086, 0x00000006, 0x000006E9,
    0x000006E7, 0x000000BD, 0x00050084, 0x00000006, 0x000006EE, 0x000006E5,
    0x00000250, 0x00050082, 0x00000006, 0x000006EF, 0x000006E3, 0x000006EE,
    0x00050084, 0x00000006, 0x000006F4, 0x000006E9, 0x000000BD, 0x00050082,
    0x00000006, 0x000006F5, 0x000006E7, 0x000006F4, 0x00050041, 0x000002CE,
    0x000006F7, 0x000002CD, 0x00000122, 0x0004003D, 0x00000006, 0x000006F8,
    0x000006F7, 0x00050084, 0x00000006, 0x000006F9, 0x000006E9, 0x000006F8,
    0x00050080, 0x00000006, 0x000006FB, 0x000006F9, 0x000006E5, 0x00050041,
    0x000002CE, 0x000006FC, 0x000002CD, 0x000000E1, 0x0004003D, 0x00000006,
    0x000006FD, 0x000006FC, 0x00050080, 0x00000006, 0x000006FF, 0x000006FD,
    0x000006FB, 0x00050041, 0x000002CE, 0x00000701, 0x000002CD, 0x00000101,
    0x0004003D, 0x00000006, 0x00000702, 0x00000701, 0x00050082, 0x00000006,
    0x00000703, 0x000006FF, 0x00000702, 0x00050041, 0x000002CE, 0x00000704,
    0x000002CD, 0x000000D5, 0x0004003D, 0x00000006, 0x00000705, 0x00000704,
    0x00050086, 0x00000006, 0x00000708, 0x00000703, 0x00000705, 0x00050084,
    0x00000006, 0x0000070C, 0x00000708, 0x00000705, 0x00050082, 0x00000006,
    0x0000070D, 0x00000703, 0x0000070C, 0x00050084, 0x00000006, 0x00000710,
    0x0000070D, 0x00000250, 0x00050080, 0x00000006, 0x00000712, 0x00000710,
    0x000006EF, 0x00050084, 0x00000006, 0x00000715, 0x00000708, 0x000000BD,
    0x00050080, 0x00000006, 0x00000717, 0x00000715, 0x000006F5, 0x000500C7,
    0x00000006, 0x000006B8, 0x00000712, 0x0000009E, 0x000500C7, 0x00000006,
    0x000006BB, 0x00000717, 0x0000009E, 0x000500C4, 0x00000006, 0x000006BC,
    0x000006BB, 0x0000009E, 0x000500C5, 0x00000006, 0x000006BD, 0x000006B8,
    0x000006BC, 0x0004003D, 0x00000310, 0x000006BE, 0x00000312, 0x000500C2,
    0x00000006, 0x000006C1, 0x00000712, 0x0000009E, 0x0004007C, 0x00000018,
    0x000006C2, 0x000006C1, 0x000500C2, 0x00000006, 0x000006C5, 0x00000717,
    0x0000009E, 0x0004007C, 0x00000018, 0x000006C6, 0x000006C5, 0x00050050,
    0x00000020, 0x000006CA, 0x000006C2, 0x000006C6, 0x0004007C, 0x00000018,
    0x000006CC, 0x000006BD, 0x0007005F, 0x00000066, 0x000006CD, 0x000006BE,
    0x000006CA, 0x00000040, 0x000006CC, 0x000300F7, 0x0000074B, 0x00000000,
    0x000700FB, 0x00000556, 0x0000072D, 0x00000005, 0x00000731, 0x00000007,
    0x00000743, 0x000200F8, 0x00000743, 0x0007004F, 0x0000029A, 0x00000745,
    0x000006CD, 0x000006CD, 0x00000000, 0x00000001, 0x0006000C, 0x00000006,
    0x00000746, 0x00000001, 0x0000003A, 0x00000745, 0x0007004F, 0x0000029A,
    0x00000748, 0x000006CD, 0x000006CD, 0x00000002, 0x00000003, 0x0006000C,
    0x00000006, 0x00000749, 0x00000001, 0x0000003A, 0x00000748, 0x00050050,
    0x00000008, 0x0000074A, 0x00000746, 0x00000749, 0x000200F9, 0x0000074B,
    0x000200F8, 0x00000731, 0x00050051, 0x00000035, 0x00000733, 0x000006CD,
    0x00000000, 0x0007000C, 0x00000035, 0x00000755, 0x00000001, 0x00000028,
    0x00000733, 0x0000026B, 0x0007000C, 0x00000035, 0x00000756, 0x00000001,
    0x00000025, 0x00000755, 0x000001CB, 0x000500BE, 0x00000034, 0x00000758,
    0x00000756, 0x00000270, 0x000600A9, 0x00000035, 0x00000759, 0x00000758,
    0x00000272, 0x00000273, 0x0008000C, 0x00000035, 0x0000075D, 0x00000001,
    0x00000032, 0x00000756, 0x00000276, 0x00000759, 0x0004006E, 0x00000018,
    0x0000075E, 0x0000075D, 0x0004007C, 0x00000006, 0x0000075F, 0x0000075E,
    0x000500C7, 0x00000006, 0x00000760, 0x0000075F, 0x00000244, 0x00050051,
    0x00000035, 0x00000736, 0x000006CD, 0x00000001, 0x0007000C, 0x00000035,
    0x00000766, 0x00000001, 0x00000028, 0x00000736, 0x0000026B, 0x0007000C,
    0x00000035, 0x00000767, 0x00000001, 0x00000025, 0x00000766, 0x000001CB,
    0x000500BE, 0x00000034, 0x00000769, 0x00000767, 0x00000270, 0x000600A9,
    0x00000035, 0x0000076A, 0x00000769, 0x00000272, 0x00000273, 0x0008000C,
    0x00000035, 0x0000076E, 0x00000001, 0x00000032, 0x00000767, 0x00000276,
    0x0000076A, 0x0004006E, 0x00000018, 0x0000076F, 0x0000076E, 0x0004007C,
    0x00000006, 0x00000770, 0x0000076F, 0x000500C7, 0x00000006, 0x00000771,
    0x00000770, 0x00000244, 0x000500C4, 0x00000006, 0x00000738, 0x00000771,
    0x000000BD, 0x000500C5, 0x00000006, 0x00000739, 0x00000760, 0x00000738,
    0x00050051, 0x00000035, 0x0000073B, 0x000006CD, 0x00000002, 0x0007000C,
    0x00000035, 0x00000777, 0x00000001, 0x00000028, 0x0000073B, 0x0000026B,
    0x0007000C, 0x00000035, 0x00000778, 0x00000001, 0x00000025, 0x00000777,
    0x000001CB, 0x000500BE, 0x00000034, 0x0000077A, 0x00000778, 0x00000270,
    0x000600A9, 0x00000035, 0x0000077B, 0x0000077A, 0x00000272, 0x00000273,
    0x0008000C, 0x00000035, 0x0000077F, 0x00000001, 0x00000032, 0x00000778,
    0x00000276, 0x0000077B, 0x0004006E, 0x00000018, 0x00000780, 0x0000077F,
    0x0004007C, 0x00000006, 0x00000781, 0x00000780, 0x000500C7, 0x00000006,
    0x00000782, 0x00000781, 0x00000244, 0x00050051, 0x00000035, 0x0000073E,
    0x000006CD, 0x00000003, 0x0007000C, 0x00000035, 0x00000788, 0x00000001,
    0x00000028, 0x0000073E, 0x0000026B, 0x0007000C, 0x00000035, 0x00000789,
    0x00000001, 0x00000025, 0x00000788, 0x000001CB, 0x000500BE, 0x00000034,
    0x0000078B, 0x00000789, 0x00000270, 0x000600A9, 0x00000035, 0x0000078C,
    0x0000078B, 0x00000272, 0x00000273, 0x0008000C, 0x00000035, 0x00000790,
    0x00000001, 0x00000032, 0x00000789, 0x00000276, 0x0000078C, 0x0004006E,
    0x00000018, 0x00000791, 0x00000790, 0x0004007C, 0x00000006, 0x00000792,
    0x00000791, 0x000500C7, 0x00000006, 0x00000793, 0x00000792, 0x00000244,
    0x000500C4, 0x00000006, 0x00000740, 0x00000793, 0x000000BD, 0x000500C5,
    0x00000006, 0x00000741, 0x00000782, 0x00000740, 0x00050050, 0x00000008,
    0x00000742, 0x00000739, 0x00000741, 0x000200F9, 0x0000074B, 0x000200F8,
    0x0000072D, 0x0007004F, 0x0000029A, 0x0000072F, 0x000006CD, 0x000006CD,
    0x00000000, 0x00000001, 0x0004007C, 0x00000008, 0x00000730, 0x0000072F,
    0x000200F9, 0x0000074B, 0x000200F8, 0x0000074B, 0x000900F5, 0x00000008,
    0x00000D7E, 0x00000730, 0x0000072D, 0x00000742, 0x00000731, 0x0000074A,
    0x00000743, 0x000300F7, 0x000007AF, 0x00000000, 0x000400FA, 0x00000678,
    0x000007A7, 0x000007A9, 0x000200F8, 0x000007A9, 0x000500AA, 0x00000034,
    0x000007AB, 0x000005AA, 0x000001A3, 0x000600A9, 0x00000006, 0x00000DBC,
    0x000007AB, 0x000000A1, 0x000000F8, 0x000200F9, 0x000007AF, 0x000200F8,
    0x000007A7, 0x000200F9, 0x000007AF, 0x000200F8, 0x000007AF, 0x000700F5,
    0x00000006, 0x00000D7F, 0x000005AA, 0x000007A7, 0x00000DBC, 0x000007A9,
    0x00050080, 0x00000006, 0x000007B9, 0x00000689, 0x0000009E, 0x00050050,
    0x00000008, 0x000007BF, 0x000007B9, 0x00000690, 0x00050080, 0x00000008,
    0x000007C2, 0x000007BF, 0x0000056C, 0x000500C4, 0x00000008, 0x000007C5,
    0x000007C2, 0x00000DAD, 0x00050050, 0x00000008, 0x000007DA, 0x00000D7F,
    0x00000D7F, 0x000500C2, 0x00000008, 0x000007D3, 0x000007DA, 0x000002FB,
    0x000500C7, 0x00000008, 0x000007D5, 0x000007D3, 0x00000DAD, 0x00050080,
    0x00000008, 0x000007C8, 0x000007C5, 0x000007D5, 0x00050051, 0x00000006,
    0x00000811, 0x000007C8, 0x00000000, 0x00050086, 0x00000006, 0x00000813,
    0x00000811, 0x00000250, 0x00050051, 0x00000006, 0x00000815, 0x000007C8,
    0x00000001, 0x00050086, 0x00000006, 0x00000817, 0x00000815, 0x000000BD,
    0x00050084, 0x00000006, 0x0000081C, 0x00000813, 0x00000250, 0x00050082,
    0x00000006, 0x0000081D, 0x00000811, 0x0000081C, 0x00050084, 0x00000006,
    0x00000822, 0x00000817, 0x000000BD, 0x00050082, 0x00000006, 0x00000823,
    0x00000815, 0x00000822, 0x00050084, 0x00000006, 0x00000827, 0x00000817,
    0x000006F8, 0x00050080, 0x00000006, 0x00000829, 0x00000827, 0x00000813,
    0x00050080, 0x00000006, 0x0000082D, 0x000006FD, 0x00000829, 0x00050082,
    0x00000006, 0x00000831, 0x0000082D, 0x00000702, 0x00050086, 0x00000006,
    0x00000836, 0x00000831, 0x00000705, 0x00050084, 0x00000006, 0x0000083A,
    0x00000836, 0x00000705, 0x00050082, 0x00000006, 0x0000083B, 0x00000831,
    0x0000083A, 0x00050084, 0x00000006, 0x0000083E, 0x0000083B, 0x00000250,
    0x00050080, 0x00000006, 0x00000840, 0x0000083E, 0x0000081D, 0x00050084,
    0x00000006, 0x00000843, 0x00000836, 0x000000BD, 0x00050080, 0x00000006,
    0x00000845, 0x00000843, 0x00000823, 0x000500C7, 0x00000006, 0x000007E6,
    0x00000840, 0x0000009E, 0x000500C7, 0x00000006, 0x000007E9, 0x00000845,
    0x0000009E, 0x000500C4, 0x00000006, 0x000007EA, 0x000007E9, 0x0000009E,
    0x000500C5, 0x00000006, 0x000007EB, 0x000007E6, 0x000007EA, 0x000500C2,
    0x00000006, 0x000007EF, 0x00000840, 0x0000009E, 0x0004007C, 0x00000018,
    0x000007F0, 0x000007EF, 0x000500C2, 0x00000006, 0x000007F3, 0x00000845,
    0x0000009E, 0x0004007C, 0x00000018, 0x000007F4, 0x000007F3, 0x00050050,
    0x00000020, 0x000007F8, 0x000007F0, 0x000007F4, 0x0004007C, 0x00000018,
    0x000007FA, 0x000007EB, 0x0007005F, 0x00000066, 0x000007FB, 0x000006BE,
    0x000007F8, 0x00000040, 0x000007FA, 0x000300F7, 0x00000879, 0x00000000,
    0x000700FB, 0x00000556, 0x0000085B, 0x00000005, 0x0000085F, 0x00000007,
    0x00000871, 0x000200F8, 0x00000871, 0x0007004F, 0x0000029A, 0x00000873,
    0x000007FB, 0x000007FB, 0x00000000, 0x00000001, 0x0006000C, 0x00000006,
    0x00000874, 0x00000001, 0x0000003A, 0x00000873, 0x0007004F, 0x0000029A,
    0x00000876, 0x000007FB, 0x000007FB, 0x00000002, 0x00000003, 0x0006000C,
    0x00000006, 0x00000877, 0x00000001, 0x0000003A, 0x00000876, 0x00050050,
    0x00000008, 0x00000878, 0x00000874, 0x00000877, 0x000200F9, 0x00000879,
    0x000200F8, 0x0000085F, 0x00050051, 0x00000035, 0x00000861, 0x000007FB,
    0x00000000, 0x0007000C, 0x00000035, 0x00000883, 0x00000001, 0x00000028,
    0x00000861, 0x0000026B, 0x0007000C, 0x00000035, 0x00000884, 0x00000001,
    0x00000025, 0x00000883, 0x000001CB, 0x000500BE, 0x00000034, 0x00000886,
    0x00000884, 0x00000270, 0x000600A9, 0x00000035, 0x00000887, 0x00000886,
    0x00000272, 0x00000273, 0x0008000C, 0x00000035, 0x0000088B, 0x00000001,
    0x00000032, 0x00000884, 0x00000276, 0x00000887, 0x0004006E, 0x00000018,
    0x0000088C, 0x0000088B, 0x0004007C, 0x00000006, 0x0000088D, 0x0000088C,
    0x000500C7, 0x00000006, 0x0000088E, 0x0000088D, 0x00000244, 0x00050051,
    0x00000035, 0x00000864, 0x000007FB, 0x00000001, 0x0007000C, 0x00000035,
    0x00000894, 0x00000001, 0x00000028, 0x00000864, 0x0000026B, 0x0007000C,
    0x00000035, 0x00000895, 0x00000001, 0x00000025, 0x00000894, 0x000001CB,
    0x000500BE, 0x00000034, 0x00000897, 0x00000895, 0x00000270, 0x000600A9,
    0x00000035, 0x00000898, 0x00000897, 0x00000272, 0x00000273, 0x0008000C,
    0x00000035, 0x0000089C, 0x00000001, 0x00000032, 0x00000895, 0x00000276,
    0x00000898, 0x0004006E, 0x00000018, 0x0000089D, 0x0000089C, 0x0004007C,
    0x00000006, 0x0000089E, 0x0000089D, 0x000500C7, 0x00000006, 0x0000089F,
    0x0000089E, 0x00000244, 0x000500C4, 0x00000006, 0x00000866, 0x0000089F,
    0x000000BD, 0x000500C5, 0x00000006, 0x00000867, 0x0000088E, 0x00000866,
    0x00050051, 0x00000035, 0x00000869, 0x000007FB, 0x00000002, 0x0007000C,
    0x00000035, 0x000008A5, 0x00000001, 0x00000028, 0x00000869, 0x0000026B,
    0x0007000C, 0x00000035, 0x000008A6, 0x00000001, 0x00000025, 0x000008A5,
    0x000001CB, 0x000500BE, 0x00000034, 0x000008A8, 0x000008A6, 0x00000270,
    0x000600A9, 0x00000035, 0x000008A9, 0x000008A8, 0x00000272, 0x00000273,
    0x0008000C, 0x00000035, 0x000008AD, 0x00000001, 0x00000032, 0x000008A6,
    0x00000276, 0x000008A9, 0x0004006E, 0x00000018, 0x000008AE, 0x000008AD,
    0x0004007C, 0x00000006, 0x000008AF, 0x000008AE, 0x000500C7, 0x00000006,
    0x000008B0, 0x000008AF, 0x00000244, 0x00050051, 0x00000035, 0x0000086C,
    0x000007FB, 0x00000003, 0x0007000C, 0x00000035, 0x000008B6, 0x00000001,
    0x00000028, 0x0000086C, 0x0000026B, 0x0007000C, 0x00000035, 0x000008B7,
    0x00000001, 0x00000025, 0x000008B6, 0x000001CB, 0x000500BE, 0x00000034,
    0x000008B9, 0x000008B7, 0x00000270, 0x000600A9, 0x00000035, 0x000008BA,
    0x000008B9, 0x00000272, 0x00000273, 0x0008000C, 0x00000035, 0x000008BE,
    0x00000001, 0x00000032, 0x000008B7, 0x00000276, 0x000008BA, 0x0004006E,
    0x00000018, 0x000008BF, 0x000008BE, 0x0004007C, 0x00000006, 0x000008C0,
    0x000008BF, 0x000500C7, 0x00000006, 0x000008C1, 0x000008C0, 0x00000244,
    0x000500C4, 0x00000006, 0x0000086E, 0x000008C1, 0x000000BD, 0x000500C5,
    0x00000006, 0x0000086F, 0x000008B0, 0x0000086E, 0x00050050, 0x00000008,
    0x00000870, 0x00000867, 0x0000086F, 0x000200F9, 0x00000879, 0x000200F8,
    0x0000085B, 0x0007004F, 0x0000029A, 0x0000085D, 0x000007FB, 0x000007FB,
    0x00000000, 0x00000001, 0x0004007C, 0x00000008, 0x0000085E, 0x0000085D,
    0x000200F9, 0x00000879, 0x000200F8, 0x00000879, 0x000900F5, 0x00000008,
    0x00000D83, 0x0000085E, 0x0000085B, 0x00000870, 0x0000085F, 0x00000878,
    0x00000871, 0x000300F7, 0x000008DD, 0x00000000, 0x000400FA, 0x00000678,
    0x000008D5, 0x000008D7, 0x000200F8, 0x000008D7, 0x000500AA, 0x00000034,
    0x000008D9, 0x000005AA, 0x000001A3, 0x000600A9, 0x00000006, 0x00000DBD,
    0x000008D9, 0x000000A1, 0x000000F8, 0x000200F9, 0x000008DD, 0x000200F8,
    0x000008D5, 0x000200F9, 0x000008DD, 0x000200F8, 0x000008DD, 0x000700F5,
    0x00000006, 0x00000D84, 0x000005AA, 0x000008D5, 0x00000DBD, 0x000008D7,
    0x00050080, 0x00000006, 0x000008E7, 0x00000689, 0x000000A1, 0x00050050,
    0x00000008, 0x000008ED, 0x000008E7, 0x00000690, 0x00050080, 0x00000008,
    0x000008F0, 0x000008ED, 0x0000056C, 0x000500C4, 0x00000008, 0x000008F3,
    0x000008F0, 0x00000DAD, 0x00050050, 0x00000008, 0x00000908, 0x00000D84,
    0x00000D84, 0x000500C2, 0x00000008, 0x00000901, 0x00000908, 0x000002FB,
    0x000500C7, 0x00000008, 0x00000903, 0x00000901, 0x00000DAD, 0x00050080,
    0x00000008, 0x000008F6, 0x000008F3, 0x00000903, 0x00050051, 0x00000006,
    0x0000093F, 0x000008F6, 0x00000000, 0x00050086, 0x00000006, 0x00000941,
    0x0000093F, 0x00000250, 0x00050051, 0x00000006, 0x00000943, 0x000008F6,
    0x00000001, 0x00050086, 0x00000006, 0x00000945, 0x00000943, 0x000000BD,
    0x00050084, 0x00000006, 0x0000094A, 0x00000941, 0x00000250, 0x00050082,
    0x00000006, 0x0000094B, 0x0000093F, 0x0000094A, 0x00050084, 0x00000006,
    0x00000950, 0x00000945, 0x000000BD, 0x00050082, 0x00000006, 0x00000951,
    0x00000943, 0x00000950, 0x00050084, 0x00000006, 0x00000955, 0x00000945,
    0x000006F8, 0x00050080, 0x00000006, 0x00000957, 0x00000955, 0x00000941,
    0x00050080, 0x00000006, 0x0000095B, 0x000006FD, 0x00000957, 0x00050082,
    0x00000006, 0x0000095F, 0x0000095B, 0x00000702, 0x00050086, 0x00000006,
    0x00000964, 0x0000095F, 0x00000705, 0x00050084, 0x00000006, 0x00000968,
    0x00000964, 0x00000705, 0x00050082, 0x00000006, 0x00000969, 0x0000095F,
    0x00000968, 0x00050084, 0x00000006, 0x0000096C, 0x00000969, 0x00000250,
    0x00050080, 0x00000006, 0x0000096E, 0x0000096C, 0x0000094B, 0x00050084,
    0x00000006, 0x00000971, 0x00000964, 0x000000BD, 0x00050080, 0x00000006,
    0x00000973, 0x00000971, 0x00000951, 0x000500C7, 0x00000006, 0x00000914,
    0x0000096E, 0x0000009E, 0x000500C7, 0x00000006, 0x00000917, 0x00000973,
    0x0000009E, 0x000500C4, 0x00000006, 0x00000918, 0x00000917, 0x0000009E,
    0x000500C5, 0x00000006, 0x00000919, 0x00000914, 0x00000918, 0x000500C2,
    0x00000006, 0x0000091D, 0x0000096E, 0x0000009E, 0x0004007C, 0x00000018,
    0x0000091E, 0x0000091D, 0x000500C2, 0x00000006, 0x00000921, 0x00000973,
    0x0000009E, 0x0004007C, 0x00000018, 0x00000922, 0x00000921, 0x00050050,
    0x00000020, 0x00000926, 0x0000091E, 0x00000922, 0x0004007C, 0x00000018,
    0x00000928, 0x00000919, 0x0007005F, 0x00000066, 0x00000929, 0x000006BE,
    0x00000926, 0x00000040, 0x00000928, 0x000300F7, 0x000009A7, 0x00000000,
    0x000700FB, 0x00000556, 0x00000989, 0x00000005, 0x0000098D, 0x00000007,
    0x0000099F, 0x000200F8, 0x0000099F, 0x0007004F, 0x0000029A, 0x000009A1,
    0x00000929, 0x00000929, 0x00000000, 0x00000001, 0x0006000C, 0x00000006,
    0x000009A2, 0x00000001, 0x0000003A, 0x000009A1, 0x0007004F, 0x0000029A,
    0x000009A4, 0x00000929, 0x00000929, 0x00000002, 0x00000003, 0x0006000C,
    0x00000006, 0x000009A5, 0x00000001, 0x0000003A, 0x000009A4, 0x00050050,
    0x00000008, 0x000009A6, 0x000009A2, 0x000009A5, 0x000200F9, 0x000009A7,
    0x000200F8, 0x0000098D, 0x00050051, 0x00000035, 0x0000098F, 0x00000929,
    0x00000000, 0x0007000C, 0x00000035, 0x000009B1, 0x00000001, 0x00000028,
    0x0000098F, 0x0000026B, 0x0007000C, 0x00000035, 0x000009B2, 0x00000001,
    0x00000025, 0x000009B1, 0x000001CB, 0x000500BE, 0x00000034, 0x000009B4,
    0x000009B2, 0x00000270, 0x000600A9, 0x00000035, 0x000009B5, 0x000009B4,
    0x00000272, 0x00000273, 0x0008000C, 0x00000035, 0x000009B9, 0x00000001,
    0x00000032, 0x000009B2, 0x00000276, 0x000009B5, 0x0004006E, 0x00000018,
    0x000009BA, 0x000009B9, 0x0004007C, 0x00000006, 0x000009BB, 0x000009BA,
    0x000500C7, 0x00000006, 0x000009BC, 0x000009BB, 0x00000244, 0x00050051,
    0x00000035, 0x00000992, 0x00000929, 0x00000001, 0x0007000C, 0x00000035,
    0x000009C2, 0x00000001, 0x00000028, 0x00000992, 0x0000026B, 0x0007000C,
    0x00000035, 0x000009C3, 0x00000001, 0x00000025, 0x000009C2, 0x000001CB,
    0x000500BE, 0x00000034, 0x000009C5, 0x000009C3, 0x00000270, 0x000600A9,
    0x00000035, 0x000009C6, 0x000009C5, 0x00000272, 0x00000273, 0x0008000C,
    0x00000035, 0x000009CA, 0x00000001, 0x00000032, 0x000009C3, 0x00000276,
    0x000009C6, 0x0004006E, 0x00000018, 0x000009CB, 0x000009CA, 0x0004007C,
    0x00000006, 0x000009CC, 0x000009CB, 0x000500C7, 0x00000006, 0x000009CD,
    0x000009CC, 0x00000244, 0x000500C4, 0x00000006, 0x00000994, 0x000009CD,
    0x000000BD, 0x000500C5, 0x00000006, 0x00000995, 0x000009BC, 0x00000994,
    0x00050051, 0x00000035, 0x00000997, 0x00000929, 0x00000002, 0x0007000C,
    0x00000035, 0x000009D3, 0x00000001, 0x00000028, 0x00000997, 0x0000026B,
    0x0007000C, 0x00000035, 0x000009D4, 0x00000001, 0x00000025, 0x000009D3,
    0x000001CB, 0x000500BE, 0x00000034, 0x000009D6, 0x000009D4, 0x00000270,
    0x000600A9, 0x00000035, 0x000009D7, 0x000009D6, 0x00000272, 0x00000273,
    0x0008000C, 0x00000035, 0x000009DB, 0x00000001, 0x00000032, 0x000009D4,
    0x00000276, 0x000009D7, 0x0004006E, 0x00000018, 0x000009DC, 0x000009DB,
    0x0004007C, 0x00000006, 0x000009DD, 0x000009DC, 0x000500C7, 0x00000006,
    0x000009DE, 0x000009DD, 0x00000244, 0x00050051, 0x00000035, 0x0000099A,
    0x00000929, 0x00000003, 0x0007000C, 0x00000035, 0x000009E4, 0x00000001,
    0x00000028, 0x0000099A, 0x0000026B, 0x0007000C, 0x00000035, 0x000009E5,
    0x00000001, 0x00000025, 0x000009E4, 0x000001CB, 0x000500BE, 0x00000034,
    0x000009E7, 0x000009E5, 0x00000270, 0x000600A9, 0x00000035, 0x000009E8,
    0x000009E7, 0x00000272, 0x00000273, 0x0008000C, 0x00000035, 0x000009EC,
    0x00000001, 0x00000032, 0x000009E5, 0x00000276, 0x000009E8, 0x0004006E,
    0x00000018, 0x000009ED, 0x000009EC, 0x0004007C, 0x00000006, 0x000009EE,
    0x000009ED, 0x000500C7, 0x00000006, 0x000009EF, 0x000009EE, 0x00000244,
    0x000500C4, 0x00000006, 0x0000099C, 0x000009EF, 0x000000BD, 0x000500C5,
    0x00000006, 0x0000099D, 0x000009DE, 0x0000099C, 0x00050050, 0x00000008,
    0x0000099E, 0x00000995, 0x0000099D, 0x000200F9, 0x000009A7, 0x000200F8,
    0x00000989, 0x0007004F, 0x0000029A, 0x0000098B, 0x00000929, 0x00000929,
    0x00000000, 0x00000001, 0x0004007C, 0x00000008, 0x0000098C, 0x0000098B,
    0x000200F9, 0x000009A7, 0x000200F8, 0x000009A7, 0x000900F5, 0x00000008,
    0x00000D88, 0x0000098C, 0x00000989, 0x0000099E, 0x0000098D, 0x000009A6,
    0x0000099F, 0x000300F7, 0x00000A0B, 0x00000000, 0x000400FA, 0x00000678,
    0x00000A03, 0x00000A05, 0x000200F8, 0x00000A05, 0x000500AA, 0x00000034,
    0x00000A07, 0x000005AA, 0x000001A3, 0x000600A9, 0x00000006, 0x00000DBE,
    0x00000A07, 0x000000A1, 0x000000F8, 0x000200F9, 0x00000A0B, 0x000200F8,
    0x00000A03, 0x000200F9, 0x00000A0B, 0x000200F8, 0x00000A0B, 0x000700F5,
    0x00000006, 0x00000D89, 0x000005AA, 0x00000A03, 0x00000DBE, 0x00000A05,
    0x00050080, 0x00000006, 0x00000A15, 0x00000689, 0x000000B7, 0x00050050,
    0x00000008, 0x00000A1B, 0x00000A15, 0x00000690, 0x00050080, 0x00000008,
    0x00000A1E, 0x00000A1B, 0x0000056C, 0x000500C4, 0x00000008, 0x00000A21,
    0x00000A1E, 0x00000DAD, 0x00050050, 0x00000008, 0x00000A36, 0x00000D89,
    0x00000D89, 0x000500C2, 0x00000008, 0x00000A2F, 0x00000A36, 0x000002FB,
    0x000500C7, 0x00000008, 0x00000A31, 0x00000A2F, 0x00000DAD, 0x00050080,
    0x00000008, 0x00000A24, 0x00000A21, 0x00000A31, 0x00050051, 0x00000006,
    0x00000A6D, 0x00000A24, 0x00000000, 0x00050086, 0x00000006, 0x00000A6F,
    0x00000A6D, 0x00000250, 0x00050051, 0x00000006, 0x00000A71, 0x00000A24,
    0x00000001, 0x00050086, 0x00000006, 0x00000A73, 0x00000A71, 0x000000BD,
    0x00050084, 0x00000006, 0x00000A78, 0x00000A6F, 0x00000250, 0x00050082,
    0x00000006, 0x00000A79, 0x00000A6D, 0x00000A78, 0x00050084, 0x00000006,
    0x00000A7E, 0x00000A73, 0x000000BD, 0x00050082, 0x00000006, 0x00000A7F,
    0x00000A71, 0x00000A7E, 0x00050084, 0x00000006, 0x00000A83, 0x00000A73,
    0x000006F8, 0x00050080, 0x00000006, 0x00000A85, 0x00000A83, 0x00000A6F,
    0x00050080, 0x00000006, 0x00000A89, 0x000006FD, 0x00000A85, 0x00050082,
    0x00000006, 0x00000A8D, 0x00000A89, 0x00000702, 0x00050086, 0x00000006,
    0x00000A92, 0x00000A8D, 0x00000705, 0x00050084, 0x00000006, 0x00000A96,
    0x00000A92, 0x00000705, 0x00050082, 0x00000006, 0x00000A97, 0x00000A8D,
    0x00000A96, 0x00050084, 0x00000006, 0x00000A9A, 0x00000A97, 0x00000250,
    0x00050080, 0x00000006, 0x00000A9C, 0x00000A9A, 0x00000A79, 0x00050084,
    0x00000006, 0x00000A9F, 0x00000A92, 0x000000BD, 0x00050080, 0x00000006,
    0x00000AA1, 0x00000A9F, 0x00000A7F, 0x000500C7, 0x00000006, 0x00000A42,
    0x00000A9C, 0x0000009E, 0x000500C7, 0x00000006, 0x00000A45, 0x00000AA1,
    0x0000009E, 0x000500C4, 0x00000006, 0x00000A46, 0x00000A45, 0x0000009E,
    0x000500C5, 0x00000006, 0x00000A47, 0x00000A42, 0x00000A46, 0x000500C2,
    0x00000006, 0x00000A4B, 0x00000A9C, 0x0000009E, 0x0004007C, 0x00000018,
    0x00000A4C, 0x00000A4B, 0x000500C2, 0x00000006, 0x00000A4F, 0x00000AA1,
    0x0000009E, 0x0004007C, 0x00000018, 0x00000A50, 0x00000A4F, 0x00050050,
    0x00000020, 0x00000A54, 0x00000A4C, 0x00000A50, 0x0004007C, 0x00000018,
    0x00000A56, 0x00000A47, 0x0007005F, 0x00000066, 0x00000A57, 0x000006BE,
    0x00000A54, 0x00000040, 0x00000A56, 0x000300F7, 0x00000AD5, 0x00000000,
    0x000700FB, 0x00000556, 0x00000AB7, 0x00000005, 0x00000ABB, 0x00000007,
    0x00000ACD, 0x000200F8, 0x00000ACD, 0x0007004F, 0x0000029A, 0x00000ACF,
    0x00000A57, 0x00000A57, 0x00000000, 0x00000001, 0x0006000C, 0x00000006,
    0x00000AD0, 0x00000001, 0x0000003A, 0x00000ACF, 0x0007004F, 0x0000029A,
    0x00000AD2, 0x00000A57, 0x00000A57, 0x00000002, 0x00000003, 0x0006000C,
    0x00000006, 0x00000AD3, 0x00000001, 0x0000003A, 0x00000AD2, 0x00050050,
    0x00000008, 0x00000AD4, 0x00000AD0, 0x00000AD3, 0x000200F9, 0x00000AD5,
    0x000200F8, 0x00000ABB, 0x00050051, 0x00000035, 0x00000ABD, 0x00000A57,
    0x00000000, 0x0007000C, 0x00000035, 0x00000ADF, 0x00000001, 0x00000028,
    0x00000ABD, 0x0000026B, 0x0007000C, 0x00000035, 0x00000AE0, 0x00000001,
    0x00000025, 0x00000ADF, 0x000001CB, 0x000500BE, 0x00000034, 0x00000AE2,
    0x00000AE0, 0x00000270, 0x000600A9, 0x00000035, 0x00000AE3, 0x00000AE2,
    0x00000272, 0x00000273, 0x0008000C, 0x00000035, 0x00000AE7, 0x00000001,
    0x00000032, 0x00000AE0, 0x00000276, 0x00000AE3, 0x0004006E, 0x00000018,
    0x00000AE8, 0x00000AE7, 0x0004007C, 0x00000006, 0x00000AE9, 0x00000AE8,
    0x000500C7, 0x00000006, 0x00000AEA, 0x00000AE9, 0x00000244, 0x00050051,
    0x00000035, 0x00000AC0, 0x00000A57, 0x00000001, 0x0007000C, 0x00000035,
    0x00000AF0, 0x00000001, 0x00000028, 0x00000AC0, 0x0000026B, 0x0007000C,
    0x00000035, 0x00000AF1, 0x00000001, 0x00000025, 0x00000AF0, 0x000001CB,
    0x000500BE, 0x00000034, 0x00000AF3, 0x00000AF1, 0x00000270, 0x000600A9,
    0x00000035, 0x00000AF4, 0x00000AF3, 0x00000272, 0x00000273, 0x0008000C,
    0x00000035, 0x00000AF8, 0x00000001, 0x00000032, 0x00000AF1, 0x00000276,
    0x00000AF4, 0x0004006E, 0x00000018, 0x00000AF9, 0x00000AF8, 0x0004007C,
    0x00000006, 0x00000AFA, 0x00000AF9, 0x000500C7, 0x00000006, 0x00000AFB,
    0x00000AFA, 0x00000244, 0x000500C4, 0x00000006, 0x00000AC2, 0x00000AFB,
    0x000000BD, 0x000500C5, 0x00000006, 0x00000AC3, 0x00000AEA, 0x00000AC2,
    0x00050051, 0x00000035, 0x00000AC5, 0x00000A57, 0x00000002, 0x0007000C,
    0x00000035, 0x00000B01, 0x00000001, 0x00000028, 0x00000AC5, 0x0000026B,
    0x0007000C, 0x00000035, 0x00000B02, 0x00000001, 0x00000025, 0x00000B01,
    0x000001CB, 0x000500BE, 0x00000034, 0x00000B04, 0x00000B02, 0x00000270,
    0x000600A9, 0x00000035, 0x00000B05, 0x00000B04, 0x00000272, 0x00000273,
    0x0008000C, 0x00000035, 0x00000B09, 0x00000001, 0x00000032, 0x00000B02,
    0x00000276, 0x00000B05, 0x0004006E, 0x00000018, 0x00000B0A, 0x00000B09,
    0x0004007C, 0x00000006, 0x00000B0B, 0x00000B0A, 0x000500C7, 0x00000006,
    0x00000B0C, 0x00000B0B, 0x00000244, 0x00050051, 0x00000035, 0x00000AC8,
    0x00000A57, 0x00000003, 0x0007000C, 0x00000035, 0x00000B12, 0x00000001,
    0x00000028, 0x00000AC8, 0x0000026B, 0x0007000C, 0x00000035, 0x00000B13,
    0x00000001, 0x00000025, 0x00000B12, 0x000001CB, 0x000500BE, 0x00000034,
    0x00000B15, 0x00000B13, 0x00000270, 0x000600A9, 0x00000035, 0x00000B16,
    0x00000B15, 0x00000272, 0x00000273, 0x0008000C, 0x00000035, 0x00000B1A,
    0x00000001, 0x00000032, 0x00000B13, 0x00000276, 0x00000B16, 0x0004006E,
    0x00000018, 0x00000B1B, 0x00000B1A, 0x0004007C, 0x00000006, 0x00000B1C,
    0x00000B1B, 0x000500C7, 0x00000006, 0x00000B1D, 0x00000B1C, 0x00000244,
    0x000500C4, 0x00000006, 0x00000ACA, 0x00000B1D, 0x000000BD, 0x000500C5,
    0x00000006, 0x00000ACB, 0x00000B0C, 0x00000ACA, 0x00050050, 0x00000008,
    0x00000ACC, 0x00000AC3, 0x00000ACB, 0x000200F9, 0x00000AD5, 0x000200F8,
    0x00000AB7, 0x0007004F, 0x0000029A, 0x00000AB9, 0x00000A57, 0x00000A57,
    0x00000000, 0x00000001, 0x0004007C, 0x00000008, 0x00000ABA, 0x00000AB9,
    0x000200F9, 0x00000AD5, 0x000200F8, 0x00000AD5, 0x000900F5, 0x00000008,
    0x00000D8D, 0x00000ABA, 0x00000AB7, 0x00000ACC, 0x00000ABB, 0x00000AD4,
    0x00000ACD, 0x00050051, 0x00000006, 0x00000502, 0x00000D7E, 0x00000000,
    0x00050051, 0x00000006, 0x00000504, 0x00000D7E, 0x00000001, 0x00050051,
    0x00000006, 0x00000506, 0x00000D83, 0x00000000, 0x00050051, 0x00000006,
    0x00000508, 0x00000D83, 0x00000001, 0x00070050, 0x0000000D, 0x00000509,
    0x00000502, 0x00000504, 0x00000506, 0x00000508, 0x00050051, 0x00000006,
    0x0000050B, 0x00000D88, 0x00000000, 0x00050051, 0x00000006, 0x0000050D,
    0x00000D88, 0x00000001, 0x00050051, 0x00000006, 0x0000050F, 0x00000D8D,
    0x00000000, 0x00050051, 0x00000006, 0x00000511, 0x00000D8D, 0x00000001,
    0x00070050, 0x0000000D, 0x00000512, 0x0000050B, 0x0000050D, 0x0000050F,
    0x00000511, 0x000500AA, 0x00000034, 0x00000B21, 0x00000689, 0x000000F8,
    0x000600A9, 0x00000034, 0x00000DBF, 0x00000B21, 0x00000335, 0x00000B21,
    0x000300F7, 0x00000B45, 0x00000002, 0x000400FA, 0x00000DBF, 0x00000B28,
    0x00000B45, 0x000200F8, 0x00000B28, 0x00060052, 0x0000000D, 0x00000D71,
    0x00000506, 0x00000509, 0x00000000, 0x00060052, 0x0000000D, 0x00000D73,
    0x00000508, 0x00000D71, 0x00000001, 0x000200F9, 0x00000B45, 0x000200F8,
    0x00000B45, 0x000700F5, 0x0000000D, 0x00000D93, 0x00000509, 0x00000AD5,
    0x00000D73, 0x00000B28, 0x000300F7, 0x00000B66, 0x00000002, 0x000400FA,
    0x00000593, 0x00000B49, 0x00000B66, 0x000200F8, 0x00000B49, 0x000500AA,
    0x00000034, 0x00000B4C, 0x00000556, 0x000001A3, 0x000400A8, 0x00000034,
    0x00000B4D, 0x00000B4C, 0x000300F7, 0x00000B52, 0x00000000, 0x000400FA,
    0x00000B4D, 0x00000B4E, 0x00000B52, 0x000200F8, 0x00000B4E, 0x000500AA,
    0x00000034, 0x00000B51, 0x00000556, 0x000001B2, 0x000200F9, 0x00000B52,
    0x000200F8, 0x00000B52, 0x000700F5, 0x00000034, 0x00000B53, 0x00000B4C,
    0x00000B49, 0x00000B51, 0x00000B4E, 0x000300F7, 0x00000B65, 0x00000002,
    0x000400FA, 0x00000B53, 0x00000B54, 0x00000B65, 0x000200F8, 0x00000B54,
    0x000500C7, 0x0000000D, 0x00000B57, 0x00000D93, 0x00000DB1, 0x0009004F,
    0x0000000D, 0x00000B59, 0x00000D93, 0x00000D93, 0x00000001, 0x00000000,
    0x00000003, 0x00000002, 0x000500C7, 0x0000000D, 0x00000B5B, 0x00000B59,
    0x00000DB2, 0x000500C5, 0x0000000D, 0x00000B5C, 0x00000B57, 0x00000B5B,
    0x000500C7, 0x0000000D, 0x00000B5F, 0x00000512, 0x00000DB1, 0x0009004F,
    0x0000000D, 0x00000B61, 0x00000512, 0x00000512, 0x00000001, 0x00000000,
    0x00000003, 0x00000002, 0x000500C7, 0x0000000D, 0x00000B63, 0x00000B61,
    0x00000DB2, 0x000500C5, 0x0000000D, 0x00000B64, 0x00000B5F, 0x00000B63,
    0x000200F9, 0x00000B65, 0x000200F8, 0x00000B65, 0x000700F5, 0x0000000D,
    0x00000D9B, 0x00000512, 0x00000B52, 0x00000B64, 0x00000B54, 0x000700F5,
    0x0000000D, 0x00000D99, 0x00000D93, 0x00000B52, 0x00000B5C, 0x00000B54,
    0x000200F9, 0x00000B66, 0x000200F8, 0x00000B66, 0x000700F5, 0x0000000D,
    0x00000D9A, 0x00000512, 0x00000B45, 0x00000D9B, 0x00000B65, 0x000700F5,
    0x0000000D, 0x00000D98, 0x00000D93, 0x00000B45, 0x00000D99, 0x00000B65,
    0x00050080, 0x00000008, 0x00000B6D, 0x00000D75, 0x000005A6, 0x000300F7,
    0x00000B81, 0x00000002, 0x000400FA, 0x0000057E, 0x00000B70, 0x00000B7B,
    0x000200F8, 0x00000B7B, 0x0004007C, 0x00000020, 0x00000B7D, 0x00000B6D,
    0x00050051, 0x00000018, 0x00000BD6, 0x00000B7D, 0x00000001, 0x000500C3,
    0x00000018, 0x00000BD7, 0x00000BD6, 0x000000E3, 0x0004007C, 0x00000018,
    0x00000BD8, 0x00000596, 0x00050084, 0x00000018, 0x00000BD9, 0x00000BD7,
    0x00000BD8, 0x00050051, 0x00000018, 0x00000BDA, 0x00000B7D, 0x00000000,
    0x000500C3, 0x00000018, 0x00000BDB, 0x00000BDA, 0x000000E3, 0x00050080,
    0x00000018, 0x00000BDC, 0x00000BD9, 0x00000BDB, 0x000500C4, 0x00000018,
    0x00000BDD, 0x00000BDC, 0x000000D7, 0x000500C3, 0x00000018, 0x00000BDF,
    0x00000BD6, 0x000000E1, 0x000500C7, 0x00000018, 0x00000BE0, 0x00000BDF,
    0x000000E7, 0x000500C4, 0x00000018, 0x00000BE1, 0x00000BE0, 0x00000101,
    0x000500C7, 0x00000018, 0x00000BE3, 0x00000BDA, 0x000000E7, 0x000500C5,
    0x00000018, 0x00000BE4, 0x00000BE1, 0x00000BE3, 0x000500C5, 0x00000018,
    0x00000BE7, 0x00000BDD, 0x00000BE4, 0x000500C4, 0x00000018, 0x00000BE8,
    0x00000BE7, 0x000000B7, 0x000500C3, 0x00000018, 0x00000BEA, 0x00000BD6,
    0x000000D5, 0x000500C7, 0x00000018, 0x00000BEB, 0x00000BEA, 0x000000E1,
    0x000500C3, 0x00000018, 0x00000BED, 0x00000BDA, 0x00000101, 0x000500C7,
    0x00000018, 0x00000BEE, 0x00000BED, 0x00000101, 0x000500C3, 0x00000018,
    0x00000BF0, 0x00000BD6, 0x00000101, 0x000500C7, 0x00000018, 0x00000BF1,
    0x00000BF0, 0x000000E1, 0x000500C4, 0x00000018, 0x00000BF2, 0x00000BF1,
    0x000000E1, 0x000500C6, 0x00000018, 0x00000BF3, 0x00000BEE, 0x00000BF2,
    0x000500C7, 0x00000018, 0x00000BF8, 0x00000BD6, 0x000000E1, 0x000500C4,
    0x00000018, 0x00000BFC, 0x00000BF8, 0x000000D5, 0x000500C4, 0x00000018,
    0x00000BFD, 0x00000BF3, 0x000000D7, 0x000500C5, 0x00000018, 0x00000BFE,
    0x00000BFC, 0x00000BFD, 0x000500C4, 0x00000018, 0x00000BFF, 0x00000BEB,
    0x000000DA, 0x000500C5, 0x00000018, 0x00000C00, 0x00000BFE, 0x00000BFF,
    0x000500C7, 0x00000018, 0x00000C01, 0x00000BE8, 0x000000DD, 0x000500C5,
    0x00000018, 0x00000C02, 0x00000C00, 0x00000C01, 0x000500C3, 0x00000018,
    0x00000C03, 0x00000BE8, 0x000000D5, 0x000500C7, 0x00000018, 0x00000C04,
    0x00000C03, 0x000000E1, 0x000500C4, 0x00000018, 0x00000C05, 0x00000C04,
    0x000000E3, 0x000500C5, 0x00000018, 0x00000C06, 0x00000C02, 0x00000C05,
    0x000500C3, 0x00000018, 0x00000C07, 0x00000BE8, 0x000000E3, 0x000500C7,
    0x00000018, 0x00000C08, 0x00000C07, 0x000000E7, 0x000500C4, 0x00000018,
    0x00000C09, 0x00000C08, 0x000000E9, 0x000500C5, 0x00000018, 0x00000C0A,
    0x00000C06, 0x00000C09, 0x000500C3, 0x00000018, 0x00000C0B, 0x00000BE8,
    0x000000E9, 0x000500C4, 0x00000018, 0x00000C0C, 0x00000C0B, 0x000000ED,
    0x000500C5, 0x00000018, 0x00000C0D, 0x00000C0A, 0x00000C0C, 0x0004007C,
    0x00000006, 0x00000B80, 0x00000C0D, 0x000200F9, 0x00000B81, 0x000200F8,
    0x00000B70, 0x00050051, 0x00000006, 0x00000B73, 0x00000B6D, 0x00000000,
    0x00050051, 0x00000006, 0x00000B74, 0x00000B6D, 0x00000001, 0x00060050,
    0x00000205, 0x00000B75, 0x00000B73, 0x00000B74, 0x00000582, 0x0004007C,
    0x00000027, 0x00000B76, 0x00000B75, 0x00050051, 0x00000018, 0x00000B8D,
    0x00000B76, 0x00000002, 0x000500C3, 0x00000018, 0x00000B8E, 0x00000B8D,
    0x00000122, 0x0004007C, 0x00000018, 0x00000B8F, 0x0000059B, 0x00050084,
    0x00000018, 0x00000B90, 0x00000B8E, 0x00000B8F, 0x00050051, 0x00000018,
    0x00000B91, 0x00000B76, 0x00000001, 0x000500C3, 0x00000018, 0x00000B92,
    0x00000B91, 0x000000D5, 0x00050080, 0x00000018, 0x00000B93, 0x00000B90,
    0x00000B92, 0x0004007C, 0x00000018, 0x00000B94, 0x00000596, 0x00050084,
    0x00000018, 0x00000B95, 0x00000B93, 0x00000B94, 0x00050051, 0x00000018,
    0x00000B96, 0x00000B76, 0x00000000, 0x000500C3, 0x00000018, 0x00000B97,
    0x00000B96, 0x000000E3, 0x00050080, 0x00000018, 0x00000B98, 0x00000B95,
    0x00000B97, 0x000500C4, 0x00000018, 0x00000B99, 0x00000B98, 0x000000E7,
    0x000500C7, 0x00000018, 0x00000B9B, 0x00000B8D, 0x00000101, 0x000500C4,
    0x00000018, 0x00000B9C, 0x00000B9B, 0x000000E3, 0x000500C3, 0x00000018,
    0x00000B9E, 0x00000B91, 0x000000E1, 0x000500C7, 0x00000018, 0x00000B9F,
    0x00000B9E, 0x00000101, 0x000500C4, 0x00000018, 0x00000BA0, 0x00000B9F,
    0x00000101, 0x000500C5, 0x00000018, 0x00000BA1, 0x00000B9C, 0x00000BA0,
    0x000500C7, 0x00000018, 0x00000BA3, 0x00000B96, 0x000000E7, 0x000500C5,
    0x00000018, 0x00000BA4, 0x00000BA1, 0x00000BA3, 0x000500C5, 0x00000018,
    0x00000BA7, 0x00000B99, 0x00000BA4, 0x000500C4, 0x00000018, 0x00000BA8,
    0x00000BA7, 0x000000B7, 0x000500C3, 0x00000018, 0x00000BAA, 0x00000B91,
    0x00000101, 0x000500C6, 0x00000018, 0x00000BAD, 0x00000BAA, 0x00000B8E,
    0x000500C7, 0x00000018, 0x00000BAE, 0x00000BAD, 0x000000E1, 0x000500C3,
    0x00000018, 0x00000BB0, 0x00000B96, 0x00000101, 0x000500C7, 0x00000018,
    0x00000BB1, 0x00000BB0, 0x00000101, 0x000500C4, 0x00000018, 0x00000BB3,
    0x00000BAE, 0x000000E1, 0x000500C6, 0x00000018, 0x00000BB4, 0x00000BB1,
    0x00000BB3, 0x000500C7, 0x00000018, 0x00000BB9, 0x00000B91, 0x000000E1,
    0x000500C4, 0x00000018, 0x00000BBD, 0x00000BB9, 0x000000D5, 0x000500C4,
    0x00000018, 0x00000BBE, 0x00000BB4, 0x000000D7, 0x000500C5, 0x00000018,
    0x00000BBF, 0x00000BBD, 0x00000BBE, 0x000500C4, 0x00000018, 0x00000BC0,
    0x00000BAE, 0x000000DA, 0x000500C5, 0x00000018, 0x00000BC1, 0x00000BBF,
    0x00000BC0, 0x000500C7, 0x00000018, 0x00000BC2, 0x00000BA8, 0x000000DD,
    0x000500C5, 0x00000018, 0x00000BC3, 0x00000BC1, 0x00000BC2, 0x000500C3,
    0x00000018, 0x00000BC4, 0x00000BA8, 0x000000D5, 0x000500C7, 0x00000018,
    0x00000BC5, 0x00000BC4, 0x000000E1, 0x000500C4, 0x00000018, 0x00000BC6,
    0x00000BC5, 0x000000E3, 0x000500C5, 0x00000018, 0x00000BC7, 0x00000BC3,
    0x00000BC6, 0x000500C3, 0x00000018, 0x00000BC8, 0x00000BA8, 0x000000E3,
    0x000500C7, 0x00000018, 0x00000BC9, 0x00000BC8, 0x000000E7, 0x000500C4,
    0x00000018, 0x00000BCA, 0x00000BC9, 0x000000E9, 0x000500C5, 0x00000018,
    0x00000BCB, 0x00000BC7, 0x00000BCA, 0x000500C3, 0x00000018, 0x00000BCC,
    0x00000BA8, 0x000000E9, 0x000500C4, 0x00000018, 0x00000BCD, 0x00000BCC,
    0x000000ED, 0x000500C5, 0x00000018, 0x00000BCE, 0x00000BCB, 0x00000BCD,
    0x0004007C, 0x00000006, 0x00000B7A, 0x00000BCE, 0x000200F9, 0x00000B81,
    0x000200F8, 0x00000B81, 0x000700F5, 0x00000006, 0x00000D9C, 0x00000B7A,
    0x00000B70, 0x00000B80, 0x00000B7B, 0x00050080, 0x00000006, 0x00000B84,
    0x00000D9C, 0x000005AD, 0x000500C2, 0x00000006, 0x00000524, 0x00000B84,
    0x000000D5, 0x000500AA, 0x00000034, 0x00000C13, 0x0000057A, 0x000000C8,
    0x000300F7, 0x00000C17, 0x00000000, 0x000400FA, 0x00000C13, 0x00000C14,
    0x00000C17, 0x000200F8, 0x00000C14, 0x0009004F, 0x0000000D, 0x00000C16,
    0x00000D98, 0x00000D98, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
    0x000200F9, 0x00000C17, 0x000200F8, 0x00000C17, 0x000700F5, 0x0000000D,
    0x00000D9E, 0x00000D98, 0x00000B81, 0x00000C16, 0x00000C14, 0x000600A9,
    0x00000006, 0x00000DC0, 0x00000C13, 0x000000A1, 0x0000057A, 0x000500AA,
    0x00000034, 0x00000C1E, 0x00000DC0, 0x0000009E, 0x000500AA, 0x00000034,
    0x00000C20, 0x00000DC0, 0x000000A1, 0x000500A6, 0x00000034, 0x00000C21,
    0x00000C1E, 0x00000C20, 0x000300F7, 0x00000C2E, 0x00000000, 0x000400FA,
    0x00000C21, 0x00000C22, 0x00000C2E, 0x000200F8, 0x00000C22, 0x000500C7,
    0x0000000D, 0x00000C25, 0x00000D9E, 0x00000DB3, 0x000500C4, 0x0000000D,
    0x00000C27, 0x00000C25, 0x00000DB4, 0x000500C7, 0x0000000D, 0x00000C2A,
    0x00000D9E, 0x00000DB5, 0x000500C2, 0x0000000D, 0x00000C2C, 0x00000C2A,
    0x00000DB4, 0x000500C5, 0x0000000D, 0x00000C2D, 0x00000C27, 0x00000C2C,
    0x000200F9, 0x00000C2E, 0x000200F8, 0x00000C2E, 0x000700F5, 0x0000000D,
    0x00000DA0, 0x00000D9E, 0x00000C17, 0x00000C2D, 0x00000C22, 0x000500AA,
    0x00000034, 0x00000C32, 0x00000DC0, 0x000000B7, 0x000500A6, 0x00000034,
    0x00000C33, 0x00000C20, 0x00000C32, 0x000300F7, 0x00000C3C, 0x00000000,
    0x000400FA, 0x00000C33, 0x00000C34, 0x00000C3C, 0x000200F8, 0x00000C34,
    0x000500C4, 0x0000000D, 0x00000C37, 0x00000DA0, 0x00000DB6, 0x000500C2,
    0x0000000D, 0x00000C3A, 0x00000DA0, 0x00000DB6, 0x000500C5, 0x0000000D,
    0x00000C3B, 0x00000C37, 0x00000C3A, 0x000200F9, 0x00000C3C, 0x000200F8,
    0x00000C3C, 0x000700F5, 0x0000000D, 0x00000DA1, 0x00000DA0, 0x00000C2E,
    0x00000C3B, 0x00000C34, 0x00060041, 0x0000047E, 0x00000529, 0x00000475,
    0x00000158, 0x00000524, 0x0003003E, 0x00000529, 0x00000DA1, 0x00050080,
    0x00000006, 0x0000052C, 0x00000B84, 0x00000DBA, 0x000500C2, 0x00000006,
    0x0000052E, 0x0000052C, 0x000000D5, 0x000300F7, 0x00000C67, 0x00000000,
    0x000400FA, 0x00000C13, 0x00000C64, 0x00000C67, 0x000200F8, 0x00000C64,
    0x0009004F, 0x0000000D, 0x00000C66, 0x00000D9A, 0x00000D9A, 0x00000001,
    0x00000000, 0x00000003, 0x00000002, 0x000200F9, 0x00000C67, 0x000200F8,
    0x00000C67, 0x000700F5, 0x0000000D, 0x00000DA9, 0x00000D9A, 0x00000C3C,
    0x00000C66, 0x00000C64, 0x000300F7, 0x00000C7E, 0x00000000, 0x000400FA,
    0x00000C21, 0x00000C72, 0x00000C7E, 0x000200F8, 0x00000C72, 0x000500C7,
    0x0000000D, 0x00000C75, 0x00000DA9, 0x00000DB3, 0x000500C4, 0x0000000D,
    0x00000C77, 0x00000C75, 0x00000DB4, 0x000500C7, 0x0000000D, 0x00000C7A,
    0x00000DA9, 0x00000DB5, 0x000500C2, 0x0000000D, 0x00000C7C, 0x00000C7A,
    0x00000DB4, 0x000500C5, 0x0000000D, 0x00000C7D, 0x00000C77, 0x00000C7C,
    0x000200F9, 0x00000C7E, 0x000200F8, 0x00000C7E, 0x000700F5, 0x0000000D,
    0x00000DAB, 0x00000DA9, 0x00000C67, 0x00000C7D, 0x00000C72, 0x000300F7,
    0x00000C8C, 0x00000000, 0x000400FA, 0x00000C33, 0x00000C84, 0x00000C8C,
    0x000200F8, 0x00000C84, 0x000500C4, 0x0000000D, 0x00000C87, 0x00000DAB,
    0x00000DB6, 0x000500C2, 0x0000000D, 0x00000C8A, 0x00000DAB, 0x00000DB6,
    0x000500C5, 0x0000000D, 0x00000C8B, 0x00000C87, 0x00000C8A, 0x000200F9,
    0x00000C8C, 0x000200F8, 0x00000C8C, 0x000700F5, 0x0000000D, 0x00000DAC,
    0x00000DAB, 0x00000C7E, 0x00000C8B, 0x00000C84, 0x00060041, 0x0000047E,
    0x00000533, 0x00000475, 0x00000158, 0x0000052E, 0x0003003E, 0x00000533,
    0x00000DAC, 0x000200F9, 0x00000534, 0x000200F8, 0x00000534, 0x000100FD,
    0x00010038,
};
