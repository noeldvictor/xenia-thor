// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Google Shaderc over Glslang; 10
; Bound: 62
; Schema: 0
               OpCapability Tessellation
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint TessellationControl %main "main" %xe_out_index %gl_InvocationID %xe_in_index %gl_TessLevelOuter %gl_TessLevelInner
               OpExecutionMode %main OutputVertices 4
               OpSource GLSL 460
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %xe_out_index "xe_out_index"
               OpName %gl_InvocationID "gl_InvocationID"
               OpName %xe_in_index "xe_in_index"
               OpName %XeSystemConstants "XeSystemConstants"
               OpMemberName %XeSystemConstants 0 "xe_vertex_index_endian"
               OpMemberName %XeSystemConstants 1 "xe_vertex_base_index"
               OpMemberName %XeSystemConstants 2 "xe_tessellation_factor_range"
               OpMemberName %XeSystemConstants 3 "xe_vertex_index_min"
               OpMemberName %XeSystemConstants 4 "xe_vertex_index_max"
               OpName %_ ""
               OpName %gl_TessLevelOuter "gl_TessLevelOuter"
               OpName %gl_TessLevelInner "gl_TessLevelInner"
               OpDecorate %xe_out_index Location 0
               OpDecorate %gl_InvocationID BuiltIn InvocationId
               OpDecorate %xe_in_index Location 0
               OpMemberDecorate %XeSystemConstants 0 Offset 8
               OpMemberDecorate %XeSystemConstants 1 Offset 12
               OpMemberDecorate %XeSystemConstants 2 Offset 384
               OpMemberDecorate %XeSystemConstants 3 Offset 392
               OpMemberDecorate %XeSystemConstants 4 Offset 396
               OpDecorate %XeSystemConstants Block
               OpDecorate %_ DescriptorSet 1
               OpDecorate %_ Binding 0
               OpDecorate %gl_TessLevelOuter Patch
               OpDecorate %gl_TessLevelOuter BuiltIn TessLevelOuter
               OpDecorate %gl_TessLevelInner Patch
               OpDecorate %gl_TessLevelInner BuiltIn TessLevelInner
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
      %float = OpTypeFloat 32
       %uint = OpTypeInt 32 0
     %uint_4 = OpConstant %uint 4
%_arr_float_uint_4 = OpTypeArray %float %uint_4
%_ptr_Output__arr_float_uint_4 = OpTypePointer Output %_arr_float_uint_4
%xe_out_index = OpVariable %_ptr_Output__arr_float_uint_4 Output
        %int = OpTypeInt 32 1
%_ptr_Input_int = OpTypePointer Input %int
%gl_InvocationID = OpVariable %_ptr_Input_int Input
    %uint_32 = OpConstant %uint 32
%_arr_float_uint_32 = OpTypeArray %float %uint_32
%_ptr_Input__arr_float_uint_32 = OpTypePointer Input %_arr_float_uint_32
%xe_in_index = OpVariable %_ptr_Input__arr_float_uint_32 Input
%_ptr_Input_float = OpTypePointer Input %float
%_ptr_Output_float = OpTypePointer Output %float
      %int_0 = OpConstant %int 0
       %bool = OpTypeBool
    %v2float = OpTypeVector %float 2
%XeSystemConstants = OpTypeStruct %uint %int %v2float %uint %uint
%_ptr_Uniform_XeSystemConstants = OpTypePointer Uniform %XeSystemConstants
          %_ = OpVariable %_ptr_Uniform_XeSystemConstants Uniform
      %int_2 = OpConstant %int 2
     %uint_1 = OpConstant %uint 1
%_ptr_Uniform_float = OpTypePointer Uniform %float
%gl_TessLevelOuter = OpVariable %_ptr_Output__arr_float_uint_4 Output
      %int_1 = OpConstant %int 1
      %int_3 = OpConstant %int 3
     %uint_2 = OpConstant %uint 2
%_arr_float_uint_2 = OpTypeArray %float %uint_2
%_ptr_Output__arr_float_uint_2 = OpTypePointer Output %_arr_float_uint_2
%gl_TessLevelInner = OpVariable %_ptr_Output__arr_float_uint_2 Output
       %main = OpFunction %void None %3
          %5 = OpLabel
         %15 = OpLoad %int %gl_InvocationID
         %22 = OpAccessChain %_ptr_Input_float %xe_in_index %15
         %23 = OpLoad %float %22
         %25 = OpAccessChain %_ptr_Output_float %xe_out_index %15
               OpStore %25 %23
         %29 = OpIEqual %bool %15 %int_0
               OpSelectionMerge %31 None
               OpBranchConditional %29 %30 %31
         %30 = OpLabel
         %41 = OpAccessChain %_ptr_Uniform_float %_ %int_2 %uint_1
         %42 = OpLoad %float %41
         %45 = OpAccessChain %_ptr_Output_float %gl_TessLevelOuter %int_0
               OpStore %45 %42
         %48 = OpAccessChain %_ptr_Output_float %gl_TessLevelOuter %int_1
               OpStore %48 %42
         %50 = OpAccessChain %_ptr_Output_float %gl_TessLevelOuter %int_2
               OpStore %50 %42
         %53 = OpAccessChain %_ptr_Output_float %gl_TessLevelOuter %int_3
               OpStore %53 %42
         %59 = OpAccessChain %_ptr_Output_float %gl_TessLevelInner %int_0
               OpStore %59 %42
         %61 = OpAccessChain %_ptr_Output_float %gl_TessLevelInner %int_1
               OpStore %61 %42
               OpBranch %31
         %31 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t tessellation_indexed_4cp_hs[] = {
    0x07230203, 0x00010000, 0x000D000A, 0x0000003E, 0x00000000, 0x00020011,
    0x00000003, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x000A000F, 0x00000001,
    0x00000004, 0x6E69616D, 0x00000000, 0x0000000B, 0x0000000E, 0x00000013,
    0x0000002B, 0x00000039, 0x00040010, 0x00000004, 0x0000001A, 0x00000004,
    0x00030003, 0x00000002, 0x000001CC, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6E69616D,
    0x00000000, 0x00060005, 0x0000000B, 0x6F5F6578, 0x695F7475, 0x7865646E,
    0x00000000, 0x00060005, 0x0000000E, 0x495F6C67, 0x636F766E, 0x6F697461,
    0x0044496E, 0x00050005, 0x00000013, 0x695F6578, 0x6E695F6E, 0x00786564,
    0x00070005, 0x00000023, 0x79536558, 0x6D657473, 0x736E6F43, 0x746E6174,
    0x00000073, 0x00090006, 0x00000023, 0x00000000, 0x765F6578, 0x65747265,
    0x6E695F78, 0x5F786564, 0x69646E65, 0x00006E61, 0x00090006, 0x00000023,
    0x00000001, 0x765F6578, 0x65747265, 0x61625F78, 0x695F6573, 0x7865646E,
    0x00000000, 0x000B0006, 0x00000023, 0x00000002, 0x745F6578, 0x65737365,
    0x74616C6C, 0x5F6E6F69, 0x74636166, 0x725F726F, 0x65676E61, 0x00000000,
    0x00080006, 0x00000023, 0x00000003, 0x765F6578, 0x65747265, 0x6E695F78,
    0x5F786564, 0x006E696D, 0x00080006, 0x00000023, 0x00000004, 0x765F6578,
    0x65747265, 0x6E695F78, 0x5F786564, 0x0078616D, 0x00030005, 0x00000025,
    0x00000000, 0x00070005, 0x0000002B, 0x545F6C67, 0x4C737365, 0x6C657665,
    0x6574754F, 0x00000072, 0x00070005, 0x00000039, 0x545F6C67, 0x4C737365,
    0x6C657665, 0x656E6E49, 0x00000072, 0x00040047, 0x0000000B, 0x0000001E,
    0x00000000, 0x00040047, 0x0000000E, 0x0000000B, 0x00000008, 0x00040047,
    0x00000013, 0x0000001E, 0x00000000, 0x00050048, 0x00000023, 0x00000000,
    0x00000023, 0x00000008, 0x00050048, 0x00000023, 0x00000001, 0x00000023,
    0x0000000C, 0x00050048, 0x00000023, 0x00000002, 0x00000023, 0x00000180,
    0x00050048, 0x00000023, 0x00000003, 0x00000023, 0x00000188, 0x00050048,
    0x00000023, 0x00000004, 0x00000023, 0x0000018C, 0x00030047, 0x00000023,
    0x00000002, 0x00040047, 0x00000025, 0x00000022, 0x00000001, 0x00040047,
    0x00000025, 0x00000021, 0x00000000, 0x00030047, 0x0000002B, 0x0000000F,
    0x00040047, 0x0000002B, 0x0000000B, 0x0000000B, 0x00030047, 0x00000039,
    0x0000000F, 0x00040047, 0x00000039, 0x0000000B, 0x0000000C, 0x00020013,
    0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000020, 0x00040015, 0x00000007, 0x00000020, 0x00000000, 0x0004002B,
    0x00000007, 0x00000008, 0x00000004, 0x0004001C, 0x00000009, 0x00000006,
    0x00000008, 0x00040020, 0x0000000A, 0x00000003, 0x00000009, 0x0004003B,
    0x0000000A, 0x0000000B, 0x00000003, 0x00040015, 0x0000000C, 0x00000020,
    0x00000001, 0x00040020, 0x0000000D, 0x00000001, 0x0000000C, 0x0004003B,
    0x0000000D, 0x0000000E, 0x00000001, 0x0004002B, 0x00000007, 0x00000010,
    0x00000020, 0x0004001C, 0x00000011, 0x00000006, 0x00000010, 0x00040020,
    0x00000012, 0x00000001, 0x00000011, 0x0004003B, 0x00000012, 0x00000013,
    0x00000001, 0x00040020, 0x00000015, 0x00000001, 0x00000006, 0x00040020,
    0x00000018, 0x00000003, 0x00000006, 0x0004002B, 0x0000000C, 0x0000001B,
    0x00000000, 0x00020014, 0x0000001C, 0x00040017, 0x00000022, 0x00000006,
    0x00000002, 0x0007001E, 0x00000023, 0x00000007, 0x0000000C, 0x00000022,
    0x00000007, 0x00000007, 0x00040020, 0x00000024, 0x00000002, 0x00000023,
    0x0004003B, 0x00000024, 0x00000025, 0x00000002, 0x0004002B, 0x0000000C,
    0x00000026, 0x00000002, 0x0004002B, 0x00000007, 0x00000027, 0x00000001,
    0x00040020, 0x00000028, 0x00000002, 0x00000006, 0x0004003B, 0x0000000A,
    0x0000002B, 0x00000003, 0x0004002B, 0x0000000C, 0x0000002E, 0x00000001,
    0x0004002B, 0x0000000C, 0x00000033, 0x00000003, 0x0004002B, 0x00000007,
    0x00000036, 0x00000002, 0x0004001C, 0x00000037, 0x00000006, 0x00000036,
    0x00040020, 0x00000038, 0x00000003, 0x00000037, 0x0004003B, 0x00000038,
    0x00000039, 0x00000003, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200F8, 0x00000005, 0x0004003D, 0x0000000C, 0x0000000F,
    0x0000000E, 0x00050041, 0x00000015, 0x00000016, 0x00000013, 0x0000000F,
    0x0004003D, 0x00000006, 0x00000017, 0x00000016, 0x00050041, 0x00000018,
    0x00000019, 0x0000000B, 0x0000000F, 0x0003003E, 0x00000019, 0x00000017,
    0x000500AA, 0x0000001C, 0x0000001D, 0x0000000F, 0x0000001B, 0x000300F7,
    0x0000001F, 0x00000000, 0x000400FA, 0x0000001D, 0x0000001E, 0x0000001F,
    0x000200F8, 0x0000001E, 0x00060041, 0x00000028, 0x00000029, 0x00000025,
    0x00000026, 0x00000027, 0x0004003D, 0x00000006, 0x0000002A, 0x00000029,
    0x00050041, 0x00000018, 0x0000002D, 0x0000002B, 0x0000001B, 0x0003003E,
    0x0000002D, 0x0000002A, 0x00050041, 0x00000018, 0x00000030, 0x0000002B,
    0x0000002E, 0x0003003E, 0x00000030, 0x0000002A, 0x00050041, 0x00000018,
    0x00000032, 0x0000002B, 0x00000026, 0x0003003E, 0x00000032, 0x0000002A,
    0x00050041, 0x00000018, 0x00000035, 0x0000002B, 0x00000033, 0x0003003E,
    0x00000035, 0x0000002A, 0x00050041, 0x00000018, 0x0000003B, 0x00000039,
    0x0000001B, 0x0003003E, 0x0000003B, 0x0000002A, 0x00050041, 0x00000018,
    0x0000003D, 0x00000039, 0x0000002E, 0x0003003E, 0x0000003D, 0x0000002A,
    0x000200F9, 0x0000001F, 0x000200F8, 0x0000001F, 0x000100FD, 0x00010038,
};
