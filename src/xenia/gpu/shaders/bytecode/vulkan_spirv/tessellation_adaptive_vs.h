// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Google Shaderc over Glslang; 10
; Bound: 111
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %main "main" %xe_out_edge_factor %gl_VertexIndex
               OpSource GLSL 460
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %xe_out_edge_factor "xe_out_edge_factor"
               OpName %gl_VertexIndex "gl_VertexIndex"
               OpName %XeSystemConstants "XeSystemConstants"
               OpMemberName %XeSystemConstants 0 "xe_vertex_index_endian"
               OpMemberName %XeSystemConstants 1 "xe_vertex_base_index"
               OpMemberName %XeSystemConstants 2 "xe_tessellation_factor_range"
               OpMemberName %XeSystemConstants 3 "xe_vertex_index_min"
               OpMemberName %XeSystemConstants 4 "xe_vertex_index_max"
               OpName %_ ""
               OpDecorate %xe_out_edge_factor Location 0
               OpDecorate %gl_VertexIndex BuiltIn VertexIndex
               OpMemberDecorate %XeSystemConstants 0 Offset 8
               OpMemberDecorate %XeSystemConstants 1 Offset 12
               OpMemberDecorate %XeSystemConstants 2 Offset 384
               OpMemberDecorate %XeSystemConstants 3 Offset 392
               OpMemberDecorate %XeSystemConstants 4 Offset 396
               OpDecorate %XeSystemConstants Block
               OpDecorate %_ DescriptorSet 1
               OpDecorate %_ Binding 0
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %uint_1 = OpConstant %uint 1
       %bool = OpTypeBool
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
     %uint_8 = OpConstant %uint 8
%uint_4278255360 = OpConstant %uint 4278255360
     %uint_3 = OpConstant %uint 3
    %uint_16 = OpConstant %uint 16
      %float = OpTypeFloat 32
%_ptr_Output_float = OpTypePointer Output %float
%xe_out_edge_factor = OpVariable %_ptr_Output_float Output
        %int = OpTypeInt 32 1
%_ptr_Input_int = OpTypePointer Input %int
%gl_VertexIndex = OpVariable %_ptr_Input_int Input
    %v2float = OpTypeVector %float 2
%XeSystemConstants = OpTypeStruct %uint %int %v2float %uint %uint
%_ptr_Uniform_XeSystemConstants = OpTypePointer Uniform %XeSystemConstants
          %_ = OpVariable %_ptr_Uniform_XeSystemConstants Uniform
      %int_0 = OpConstant %int 0
%_ptr_Uniform_uint = OpTypePointer Uniform %uint
    %float_1 = OpConstant %float 1
      %int_2 = OpConstant %int 2
     %uint_0 = OpConstant %uint 0
%_ptr_Uniform_float = OpTypePointer Uniform %float
       %main = OpFunction %void None %3
          %5 = OpLabel
         %56 = OpLoad %int %gl_VertexIndex
         %57 = OpBitcast %uint %56
         %66 = OpAccessChain %_ptr_Uniform_uint %_ %int_0
         %67 = OpLoad %uint %66
         %83 = OpIEqual %bool %67 %uint_1
         %85 = OpIEqual %bool %67 %uint_2
         %86 = OpLogicalOr %bool %83 %85
               OpSelectionMerge %95 None
               OpBranchConditional %86 %87 %95
         %87 = OpLabel
         %89 = OpBitwiseAnd %uint %57 %uint_16711935
         %90 = OpShiftLeftLogical %uint %89 %uint_8
         %92 = OpBitwiseAnd %uint %57 %uint_4278255360
         %93 = OpShiftRightLogical %uint %92 %uint_8
         %94 = OpBitwiseOr %uint %90 %93
               OpBranch %95
         %95 = OpLabel
        %109 = OpPhi %uint %57 %5 %94 %87
         %99 = OpIEqual %bool %67 %uint_3
        %100 = OpLogicalOr %bool %85 %99
               OpSelectionMerge %107 None
               OpBranchConditional %100 %101 %107
        %101 = OpLabel
        %103 = OpShiftLeftLogical %uint %109 %uint_16
        %105 = OpShiftRightLogical %uint %109 %uint_16
        %106 = OpBitwiseOr %uint %103 %105
               OpBranch %107
        %107 = OpLabel
        %110 = OpPhi %uint %109 %95 %106 %101
         %69 = OpBitcast %float %110
         %71 = OpFAdd %float %69 %float_1
         %75 = OpAccessChain %_ptr_Uniform_float %_ %int_2 %uint_0
         %76 = OpLoad %float %75
         %77 = OpAccessChain %_ptr_Uniform_float %_ %int_2 %uint_1
         %78 = OpLoad %float %77
         %79 = OpExtInst %float %1 FClamp %71 %76 %78
               OpStore %xe_out_edge_factor %79
               OpReturn
               OpFunctionEnd
#endif

const uint32_t tessellation_adaptive_vs[] = {
    0x07230203, 0x00010000, 0x000D000A, 0x0000006F, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0007000F, 0x00000000,
    0x00000004, 0x6E69616D, 0x00000000, 0x00000034, 0x00000037, 0x00030003,
    0x00000002, 0x000001CC, 0x000A0004, 0x475F4C47, 0x4C474F4F, 0x70635F45,
    0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365, 0x00006576,
    0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63, 0x69645F65,
    0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6E69616D, 0x00000000,
    0x00070005, 0x00000034, 0x6F5F6578, 0x655F7475, 0x5F656764, 0x74636166,
    0x0000726F, 0x00060005, 0x00000037, 0x565F6C67, 0x65747265, 0x646E4978,
    0x00007865, 0x00070005, 0x0000003B, 0x79536558, 0x6D657473, 0x736E6F43,
    0x746E6174, 0x00000073, 0x00090006, 0x0000003B, 0x00000000, 0x765F6578,
    0x65747265, 0x6E695F78, 0x5F786564, 0x69646E65, 0x00006E61, 0x00090006,
    0x0000003B, 0x00000001, 0x765F6578, 0x65747265, 0x61625F78, 0x695F6573,
    0x7865646E, 0x00000000, 0x000B0006, 0x0000003B, 0x00000002, 0x745F6578,
    0x65737365, 0x74616C6C, 0x5F6E6F69, 0x74636166, 0x725F726F, 0x65676E61,
    0x00000000, 0x00080006, 0x0000003B, 0x00000003, 0x765F6578, 0x65747265,
    0x6E695F78, 0x5F786564, 0x006E696D, 0x00080006, 0x0000003B, 0x00000004,
    0x765F6578, 0x65747265, 0x6E695F78, 0x5F786564, 0x0078616D, 0x00030005,
    0x0000003D, 0x00000000, 0x00040047, 0x00000034, 0x0000001E, 0x00000000,
    0x00040047, 0x00000037, 0x0000000B, 0x0000002A, 0x00050048, 0x0000003B,
    0x00000000, 0x00000023, 0x00000008, 0x00050048, 0x0000003B, 0x00000001,
    0x00000023, 0x0000000C, 0x00050048, 0x0000003B, 0x00000002, 0x00000023,
    0x00000180, 0x00050048, 0x0000003B, 0x00000003, 0x00000023, 0x00000188,
    0x00050048, 0x0000003B, 0x00000004, 0x00000023, 0x0000018C, 0x00030047,
    0x0000003B, 0x00000002, 0x00040047, 0x0000003D, 0x00000022, 0x00000001,
    0x00040047, 0x0000003D, 0x00000021, 0x00000000, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020,
    0x00000000, 0x0004002B, 0x00000006, 0x0000000E, 0x00000001, 0x00020014,
    0x0000000F, 0x0004002B, 0x00000006, 0x00000012, 0x00000002, 0x0004002B,
    0x00000006, 0x00000018, 0x00FF00FF, 0x0004002B, 0x00000006, 0x0000001A,
    0x00000008, 0x0004002B, 0x00000006, 0x0000001D, 0xFF00FF00, 0x0004002B,
    0x00000006, 0x00000024, 0x00000003, 0x0004002B, 0x00000006, 0x0000002A,
    0x00000010, 0x00030016, 0x00000032, 0x00000020, 0x00040020, 0x00000033,
    0x00000003, 0x00000032, 0x0004003B, 0x00000033, 0x00000034, 0x00000003,
    0x00040015, 0x00000035, 0x00000020, 0x00000001, 0x00040020, 0x00000036,
    0x00000001, 0x00000035, 0x0004003B, 0x00000036, 0x00000037, 0x00000001,
    0x00040017, 0x0000003A, 0x00000032, 0x00000002, 0x0007001E, 0x0000003B,
    0x00000006, 0x00000035, 0x0000003A, 0x00000006, 0x00000006, 0x00040020,
    0x0000003C, 0x00000002, 0x0000003B, 0x0004003B, 0x0000003C, 0x0000003D,
    0x00000002, 0x0004002B, 0x00000035, 0x0000003E, 0x00000000, 0x00040020,
    0x00000041, 0x00000002, 0x00000006, 0x0004002B, 0x00000032, 0x00000046,
    0x3F800000, 0x0004002B, 0x00000035, 0x00000048, 0x00000002, 0x0004002B,
    0x00000006, 0x00000049, 0x00000000, 0x00040020, 0x0000004A, 0x00000002,
    0x00000032, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200F8, 0x00000005, 0x0004003D, 0x00000035, 0x00000038, 0x00000037,
    0x0004007C, 0x00000006, 0x00000039, 0x00000038, 0x00050041, 0x00000041,
    0x00000042, 0x0000003D, 0x0000003E, 0x0004003D, 0x00000006, 0x00000043,
    0x00000042, 0x000500AA, 0x0000000F, 0x00000053, 0x00000043, 0x0000000E,
    0x000500AA, 0x0000000F, 0x00000055, 0x00000043, 0x00000012, 0x000500A6,
    0x0000000F, 0x00000056, 0x00000053, 0x00000055, 0x000300F7, 0x0000005F,
    0x00000000, 0x000400FA, 0x00000056, 0x00000057, 0x0000005F, 0x000200F8,
    0x00000057, 0x000500C7, 0x00000006, 0x00000059, 0x00000039, 0x00000018,
    0x000500C4, 0x00000006, 0x0000005A, 0x00000059, 0x0000001A, 0x000500C7,
    0x00000006, 0x0000005C, 0x00000039, 0x0000001D, 0x000500C2, 0x00000006,
    0x0000005D, 0x0000005C, 0x0000001A, 0x000500C5, 0x00000006, 0x0000005E,
    0x0000005A, 0x0000005D, 0x000200F9, 0x0000005F, 0x000200F8, 0x0000005F,
    0x000700F5, 0x00000006, 0x0000006D, 0x00000039, 0x00000005, 0x0000005E,
    0x00000057, 0x000500AA, 0x0000000F, 0x00000063, 0x00000043, 0x00000024,
    0x000500A6, 0x0000000F, 0x00000064, 0x00000055, 0x00000063, 0x000300F7,
    0x0000006B, 0x00000000, 0x000400FA, 0x00000064, 0x00000065, 0x0000006B,
    0x000200F8, 0x00000065, 0x000500C4, 0x00000006, 0x00000067, 0x0000006D,
    0x0000002A, 0x000500C2, 0x00000006, 0x00000069, 0x0000006D, 0x0000002A,
    0x000500C5, 0x00000006, 0x0000006A, 0x00000067, 0x00000069, 0x000200F9,
    0x0000006B, 0x000200F8, 0x0000006B, 0x000700F5, 0x00000006, 0x0000006E,
    0x0000006D, 0x0000005F, 0x0000006A, 0x00000065, 0x0004007C, 0x00000032,
    0x00000045, 0x0000006E, 0x00050081, 0x00000032, 0x00000047, 0x00000045,
    0x00000046, 0x00060041, 0x0000004A, 0x0000004B, 0x0000003D, 0x00000048,
    0x00000049, 0x0004003D, 0x00000032, 0x0000004C, 0x0000004B, 0x00060041,
    0x0000004A, 0x0000004D, 0x0000003D, 0x00000048, 0x0000000E, 0x0004003D,
    0x00000032, 0x0000004E, 0x0000004D, 0x0008000C, 0x00000032, 0x0000004F,
    0x00000001, 0x0000002B, 0x00000047, 0x0000004C, 0x0000004E, 0x0003003E,
    0x00000034, 0x0000004F, 0x000100FD, 0x00010038,
};
