// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Google Shaderc over Glslang; 10
; Bound: 140
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %main "main" %xe_out_index %gl_VertexIndex
               OpSource GLSL 460
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %XeSystemConstants "XeSystemConstants"
               OpMemberName %XeSystemConstants 0 "xe_vertex_index_endian"
               OpMemberName %XeSystemConstants 1 "xe_vertex_base_index"
               OpMemberName %XeSystemConstants 2 "xe_tessellation_factor_range"
               OpMemberName %XeSystemConstants 3 "xe_vertex_index_min"
               OpMemberName %XeSystemConstants 4 "xe_vertex_index_max"
               OpName %_ ""
               OpName %xe_out_index "xe_out_index"
               OpName %gl_VertexIndex "gl_VertexIndex"
               OpMemberDecorate %XeSystemConstants 0 Offset 8
               OpMemberDecorate %XeSystemConstants 1 Offset 12
               OpMemberDecorate %XeSystemConstants 2 Offset 384
               OpMemberDecorate %XeSystemConstants 3 Offset 392
               OpMemberDecorate %XeSystemConstants 4 Offset 396
               OpDecorate %XeSystemConstants Block
               OpDecorate %_ DescriptorSet 1
               OpDecorate %_ Binding 0
               OpDecorate %xe_out_index Location 0
               OpDecorate %gl_VertexIndex BuiltIn VertexIndex
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
      %float = OpTypeFloat 32
     %uint_1 = OpConstant %uint 1
       %bool = OpTypeBool
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
     %uint_8 = OpConstant %uint 8
%uint_4278255360 = OpConstant %uint 4278255360
     %uint_3 = OpConstant %uint 3
    %uint_16 = OpConstant %uint 16
        %int = OpTypeInt 32 1
    %v2float = OpTypeVector %float 2
%XeSystemConstants = OpTypeStruct %uint %int %v2float %uint %uint
%_ptr_Uniform_XeSystemConstants = OpTypePointer Uniform %XeSystemConstants
          %_ = OpVariable %_ptr_Uniform_XeSystemConstants Uniform
      %int_1 = OpConstant %int 1
%_ptr_Uniform_int = OpTypePointer Uniform %int
%uint_16777215 = OpConstant %uint 16777215
      %int_3 = OpConstant %int 3
%_ptr_Uniform_uint = OpTypePointer Uniform %uint
      %int_4 = OpConstant %int 4
%_ptr_Output_float = OpTypePointer Output %float
%xe_out_index = OpVariable %_ptr_Output_float Output
%_ptr_Input_int = OpTypePointer Input %int
%gl_VertexIndex = OpVariable %_ptr_Input_int Input
      %int_0 = OpConstant %int 0
       %main = OpFunction %void None %3
          %5 = OpLabel
         %84 = OpLoad %int %gl_VertexIndex
         %85 = OpBitcast %uint %84
         %89 = OpAccessChain %_ptr_Uniform_uint %_ %int_0
         %90 = OpLoad %uint %89
         %97 = OpIEqual %bool %90 %uint_1
         %99 = OpIEqual %bool %90 %uint_2
        %100 = OpLogicalOr %bool %97 %99
               OpSelectionMerge %109 None
               OpBranchConditional %100 %101 %109
        %101 = OpLabel
        %103 = OpBitwiseAnd %uint %85 %uint_16711935
        %104 = OpShiftLeftLogical %uint %103 %uint_8
        %106 = OpBitwiseAnd %uint %85 %uint_4278255360
        %107 = OpShiftRightLogical %uint %106 %uint_8
        %108 = OpBitwiseOr %uint %104 %107
               OpBranch %109
        %109 = OpLabel
        %138 = OpPhi %uint %85 %5 %108 %101
        %113 = OpIEqual %bool %90 %uint_3
        %114 = OpLogicalOr %bool %99 %113
               OpSelectionMerge %121 None
               OpBranchConditional %114 %115 %121
        %115 = OpLabel
        %117 = OpShiftLeftLogical %uint %138 %uint_16
        %119 = OpShiftRightLogical %uint %138 %uint_16
        %120 = OpBitwiseOr %uint %117 %119
               OpBranch %121
        %121 = OpLabel
        %139 = OpPhi %uint %138 %109 %120 %115
        %127 = OpAccessChain %_ptr_Uniform_int %_ %int_1
        %128 = OpLoad %int %127
        %129 = OpBitcast %uint %128
        %130 = OpIAdd %uint %139 %129
        %131 = OpBitwiseAnd %uint %130 %uint_16777215
        %132 = OpAccessChain %_ptr_Uniform_uint %_ %int_3
        %133 = OpLoad %uint %132
        %134 = OpAccessChain %_ptr_Uniform_uint %_ %int_4
        %135 = OpLoad %uint %134
        %136 = OpExtInst %uint %1 UClamp %131 %133 %135
        %137 = OpConvertUToF %float %136
               OpStore %xe_out_index %137
               OpReturn
               OpFunctionEnd
#endif

const uint32_t tessellation_indexed_vs[] = {
    0x07230203, 0x00010000, 0x000D000A, 0x0000008C, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0007000F, 0x00000000,
    0x00000004, 0x6E69616D, 0x00000000, 0x00000051, 0x00000053, 0x00030003,
    0x00000002, 0x000001CC, 0x000A0004, 0x475F4C47, 0x4C474F4F, 0x70635F45,
    0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365, 0x00006576,
    0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63, 0x69645F65,
    0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6E69616D, 0x00000000,
    0x00070005, 0x0000003A, 0x79536558, 0x6D657473, 0x736E6F43, 0x746E6174,
    0x00000073, 0x00090006, 0x0000003A, 0x00000000, 0x765F6578, 0x65747265,
    0x6E695F78, 0x5F786564, 0x69646E65, 0x00006E61, 0x00090006, 0x0000003A,
    0x00000001, 0x765F6578, 0x65747265, 0x61625F78, 0x695F6573, 0x7865646E,
    0x00000000, 0x000B0006, 0x0000003A, 0x00000002, 0x745F6578, 0x65737365,
    0x74616C6C, 0x5F6E6F69, 0x74636166, 0x725F726F, 0x65676E61, 0x00000000,
    0x00080006, 0x0000003A, 0x00000003, 0x765F6578, 0x65747265, 0x6E695F78,
    0x5F786564, 0x006E696D, 0x00080006, 0x0000003A, 0x00000004, 0x765F6578,
    0x65747265, 0x6E695F78, 0x5F786564, 0x0078616D, 0x00030005, 0x0000003C,
    0x00000000, 0x00060005, 0x00000051, 0x6F5F6578, 0x695F7475, 0x7865646E,
    0x00000000, 0x00060005, 0x00000053, 0x565F6C67, 0x65747265, 0x646E4978,
    0x00007865, 0x00050048, 0x0000003A, 0x00000000, 0x00000023, 0x00000008,
    0x00050048, 0x0000003A, 0x00000001, 0x00000023, 0x0000000C, 0x00050048,
    0x0000003A, 0x00000002, 0x00000023, 0x00000180, 0x00050048, 0x0000003A,
    0x00000003, 0x00000023, 0x00000188, 0x00050048, 0x0000003A, 0x00000004,
    0x00000023, 0x0000018C, 0x00030047, 0x0000003A, 0x00000002, 0x00040047,
    0x0000003C, 0x00000022, 0x00000001, 0x00040047, 0x0000003C, 0x00000021,
    0x00000000, 0x00040047, 0x00000051, 0x0000001E, 0x00000000, 0x00040047,
    0x00000053, 0x0000000B, 0x0000002A, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020, 0x00000000,
    0x00030016, 0x0000000D, 0x00000020, 0x0004002B, 0x00000006, 0x00000013,
    0x00000001, 0x00020014, 0x00000014, 0x0004002B, 0x00000006, 0x00000017,
    0x00000002, 0x0004002B, 0x00000006, 0x0000001D, 0x00FF00FF, 0x0004002B,
    0x00000006, 0x0000001F, 0x00000008, 0x0004002B, 0x00000006, 0x00000022,
    0xFF00FF00, 0x0004002B, 0x00000006, 0x00000029, 0x00000003, 0x0004002B,
    0x00000006, 0x0000002F, 0x00000010, 0x00040015, 0x00000038, 0x00000020,
    0x00000001, 0x00040017, 0x00000039, 0x0000000D, 0x00000002, 0x0007001E,
    0x0000003A, 0x00000006, 0x00000038, 0x00000039, 0x00000006, 0x00000006,
    0x00040020, 0x0000003B, 0x00000002, 0x0000003A, 0x0004003B, 0x0000003B,
    0x0000003C, 0x00000002, 0x0004002B, 0x00000038, 0x0000003D, 0x00000001,
    0x00040020, 0x0000003E, 0x00000002, 0x00000038, 0x0004002B, 0x00000006,
    0x00000043, 0x00FFFFFF, 0x0004002B, 0x00000038, 0x00000045, 0x00000003,
    0x00040020, 0x00000046, 0x00000002, 0x00000006, 0x0004002B, 0x00000038,
    0x00000049, 0x00000004, 0x00040020, 0x00000050, 0x00000003, 0x0000000D,
    0x0004003B, 0x00000050, 0x00000051, 0x00000003, 0x00040020, 0x00000052,
    0x00000001, 0x00000038, 0x0004003B, 0x00000052, 0x00000053, 0x00000001,
    0x0004002B, 0x00000038, 0x00000056, 0x00000000, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0004003D,
    0x00000038, 0x00000054, 0x00000053, 0x0004007C, 0x00000006, 0x00000055,
    0x00000054, 0x00050041, 0x00000046, 0x00000059, 0x0000003C, 0x00000056,
    0x0004003D, 0x00000006, 0x0000005A, 0x00000059, 0x000500AA, 0x00000014,
    0x00000061, 0x0000005A, 0x00000013, 0x000500AA, 0x00000014, 0x00000063,
    0x0000005A, 0x00000017, 0x000500A6, 0x00000014, 0x00000064, 0x00000061,
    0x00000063, 0x000300F7, 0x0000006D, 0x00000000, 0x000400FA, 0x00000064,
    0x00000065, 0x0000006D, 0x000200F8, 0x00000065, 0x000500C7, 0x00000006,
    0x00000067, 0x00000055, 0x0000001D, 0x000500C4, 0x00000006, 0x00000068,
    0x00000067, 0x0000001F, 0x000500C7, 0x00000006, 0x0000006A, 0x00000055,
    0x00000022, 0x000500C2, 0x00000006, 0x0000006B, 0x0000006A, 0x0000001F,
    0x000500C5, 0x00000006, 0x0000006C, 0x00000068, 0x0000006B, 0x000200F9,
    0x0000006D, 0x000200F8, 0x0000006D, 0x000700F5, 0x00000006, 0x0000008A,
    0x00000055, 0x00000005, 0x0000006C, 0x00000065, 0x000500AA, 0x00000014,
    0x00000071, 0x0000005A, 0x00000029, 0x000500A6, 0x00000014, 0x00000072,
    0x00000063, 0x00000071, 0x000300F7, 0x00000079, 0x00000000, 0x000400FA,
    0x00000072, 0x00000073, 0x00000079, 0x000200F8, 0x00000073, 0x000500C4,
    0x00000006, 0x00000075, 0x0000008A, 0x0000002F, 0x000500C2, 0x00000006,
    0x00000077, 0x0000008A, 0x0000002F, 0x000500C5, 0x00000006, 0x00000078,
    0x00000075, 0x00000077, 0x000200F9, 0x00000079, 0x000200F8, 0x00000079,
    0x000700F5, 0x00000006, 0x0000008B, 0x0000008A, 0x0000006D, 0x00000078,
    0x00000073, 0x00050041, 0x0000003E, 0x0000007F, 0x0000003C, 0x0000003D,
    0x0004003D, 0x00000038, 0x00000080, 0x0000007F, 0x0004007C, 0x00000006,
    0x00000081, 0x00000080, 0x00050080, 0x00000006, 0x00000082, 0x0000008B,
    0x00000081, 0x000500C7, 0x00000006, 0x00000083, 0x00000082, 0x00000043,
    0x00050041, 0x00000046, 0x00000084, 0x0000003C, 0x00000045, 0x0004003D,
    0x00000006, 0x00000085, 0x00000084, 0x00050041, 0x00000046, 0x00000086,
    0x0000003C, 0x00000049, 0x0004003D, 0x00000006, 0x00000087, 0x00000086,
    0x0008000C, 0x00000006, 0x00000088, 0x00000001, 0x0000002C, 0x00000083,
    0x00000085, 0x00000087, 0x00040070, 0x0000000D, 0x00000089, 0x00000088,
    0x0003003E, 0x00000051, 0x00000089, 0x000100FD, 0x00010038,
};
