// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Google Shaderc over Glslang; 10
; Bound: 110
; Schema: 0
               OpCapability Tessellation
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint TessellationControl %main "main" %xe_out_index %gl_InvocationID %gl_PrimitiveID %xe_in_edge_factor %gl_TessLevelOuter %gl_TessLevelInner
               OpExecutionMode %main OutputVertices 1
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
               OpName %gl_InvocationID "gl_InvocationID"
               OpName %gl_PrimitiveID "gl_PrimitiveID"
               OpName %xe_in_edge_factor "xe_in_edge_factor"
               OpName %gl_TessLevelOuter "gl_TessLevelOuter"
               OpName %gl_TessLevelInner "gl_TessLevelInner"
               OpMemberDecorate %XeSystemConstants 0 Offset 8
               OpMemberDecorate %XeSystemConstants 1 Offset 12
               OpMemberDecorate %XeSystemConstants 2 Offset 384
               OpMemberDecorate %XeSystemConstants 3 Offset 392
               OpMemberDecorate %XeSystemConstants 4 Offset 396
               OpDecorate %XeSystemConstants Block
               OpDecorate %_ DescriptorSet 1
               OpDecorate %_ Binding 0
               OpDecorate %xe_out_index Location 0
               OpDecorate %gl_InvocationID BuiltIn InvocationId
               OpDecorate %gl_PrimitiveID BuiltIn PrimitiveId
               OpDecorate %xe_in_edge_factor Location 0
               OpDecorate %gl_TessLevelOuter Patch
               OpDecorate %gl_TessLevelOuter BuiltIn TessLevelOuter
               OpDecorate %gl_TessLevelInner Patch
               OpDecorate %gl_TessLevelInner BuiltIn TessLevelInner
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
      %float = OpTypeFloat 32
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
     %uint_1 = OpConstant %uint 1
%_arr_float_uint_1 = OpTypeArray %float %uint_1
%_ptr_Output__arr_float_uint_1 = OpTypePointer Output %_arr_float_uint_1
%xe_out_index = OpVariable %_ptr_Output__arr_float_uint_1 Output
%_ptr_Input_int = OpTypePointer Input %int
%gl_InvocationID = OpVariable %_ptr_Input_int Input
%gl_PrimitiveID = OpVariable %_ptr_Input_int Input
%_ptr_Output_float = OpTypePointer Output %float
    %uint_32 = OpConstant %uint 32
%_arr_float_uint_32 = OpTypeArray %float %uint_32
%_ptr_Input__arr_float_uint_32 = OpTypePointer Input %_arr_float_uint_32
%xe_in_edge_factor = OpVariable %_ptr_Input__arr_float_uint_32 Input
%_ptr_Input_float = OpTypePointer Input %float
      %int_0 = OpConstant %int 0
      %int_2 = OpConstant %int 2
     %uint_4 = OpConstant %uint 4
%_arr_float_uint_4 = OpTypeArray %float %uint_4
%_ptr_Output__arr_float_uint_4 = OpTypePointer Output %_arr_float_uint_4
%gl_TessLevelOuter = OpVariable %_ptr_Output__arr_float_uint_4 Output
     %uint_2 = OpConstant %uint 2
%_arr_float_uint_2 = OpTypeArray %float %uint_2
%_ptr_Output__arr_float_uint_2 = OpTypePointer Output %_arr_float_uint_2
%gl_TessLevelInner = OpVariable %_ptr_Output__arr_float_uint_2 Output
       %main = OpFunction %void None %3
          %5 = OpLabel
         %44 = OpLoad %int %gl_InvocationID
         %46 = OpLoad %int %gl_PrimitiveID
         %47 = OpBitcast %uint %46
         %99 = OpAccessChain %_ptr_Uniform_int %_ %int_1
        %100 = OpLoad %int %99
        %101 = OpBitcast %uint %100
        %102 = OpIAdd %uint %47 %101
        %103 = OpBitwiseAnd %uint %102 %uint_16777215
        %104 = OpAccessChain %_ptr_Uniform_uint %_ %int_3
        %105 = OpLoad %uint %104
        %106 = OpAccessChain %_ptr_Uniform_uint %_ %int_4
        %107 = OpLoad %uint %106
        %108 = OpExtInst %uint %1 UClamp %103 %105 %107
        %109 = OpConvertUToF %float %108
         %51 = OpAccessChain %_ptr_Output_float %xe_out_index %44
               OpStore %51 %109
         %59 = OpAccessChain %_ptr_Input_float %xe_in_edge_factor %int_3
         %60 = OpLoad %float %59
         %63 = OpAccessChain %_ptr_Input_float %xe_in_edge_factor %int_0
         %64 = OpLoad %float %63
         %66 = OpAccessChain %_ptr_Input_float %xe_in_edge_factor %int_1
         %67 = OpLoad %float %66
         %70 = OpAccessChain %_ptr_Input_float %xe_in_edge_factor %int_2
         %71 = OpLoad %float %70
         %77 = OpAccessChain %_ptr_Output_float %gl_TessLevelOuter %int_0
               OpStore %77 %60
         %79 = OpAccessChain %_ptr_Output_float %gl_TessLevelOuter %int_1
               OpStore %79 %64
         %81 = OpAccessChain %_ptr_Output_float %gl_TessLevelOuter %int_2
               OpStore %81 %67
         %83 = OpAccessChain %_ptr_Output_float %gl_TessLevelOuter %int_3
               OpStore %83 %71
         %90 = OpExtInst %float %1 FMin %64 %71
         %91 = OpAccessChain %_ptr_Output_float %gl_TessLevelInner %int_0
               OpStore %91 %90
         %94 = OpExtInst %float %1 FMin %60 %67
         %95 = OpAccessChain %_ptr_Output_float %gl_TessLevelInner %int_1
               OpStore %95 %94
               OpReturn
               OpFunctionEnd
#endif

const uint32_t tessellation_adaptive_quad_hs[] = {
    0x07230203, 0x00010000, 0x000D000A, 0x0000006E, 0x00000000, 0x00020011,
    0x00000003, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x000B000F, 0x00000001,
    0x00000004, 0x6E69616D, 0x00000000, 0x00000029, 0x0000002B, 0x0000002D,
    0x00000039, 0x0000004B, 0x00000057, 0x00040010, 0x00000004, 0x0000001A,
    0x00000001, 0x00030003, 0x00000002, 0x000001CC, 0x000A0004, 0x475F4C47,
    0x4C474F4F, 0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F,
    0x69746365, 0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45,
    0x64756C63, 0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x00000004,
    0x6E69616D, 0x00000000, 0x00070005, 0x00000010, 0x79536558, 0x6D657473,
    0x736E6F43, 0x746E6174, 0x00000073, 0x00090006, 0x00000010, 0x00000000,
    0x765F6578, 0x65747265, 0x6E695F78, 0x5F786564, 0x69646E65, 0x00006E61,
    0x00090006, 0x00000010, 0x00000001, 0x765F6578, 0x65747265, 0x61625F78,
    0x695F6573, 0x7865646E, 0x00000000, 0x000B0006, 0x00000010, 0x00000002,
    0x745F6578, 0x65737365, 0x74616C6C, 0x5F6E6F69, 0x74636166, 0x725F726F,
    0x65676E61, 0x00000000, 0x00080006, 0x00000010, 0x00000003, 0x765F6578,
    0x65747265, 0x6E695F78, 0x5F786564, 0x006E696D, 0x00080006, 0x00000010,
    0x00000004, 0x765F6578, 0x65747265, 0x6E695F78, 0x5F786564, 0x0078616D,
    0x00030005, 0x00000012, 0x00000000, 0x00060005, 0x00000029, 0x6F5F6578,
    0x695F7475, 0x7865646E, 0x00000000, 0x00060005, 0x0000002B, 0x495F6C67,
    0x636F766E, 0x6F697461, 0x0044496E, 0x00060005, 0x0000002D, 0x505F6C67,
    0x696D6972, 0x65766974, 0x00004449, 0x00070005, 0x00000039, 0x695F6578,
    0x64655F6E, 0x665F6567, 0x6F746361, 0x00000072, 0x00070005, 0x0000004B,
    0x545F6C67, 0x4C737365, 0x6C657665, 0x6574754F, 0x00000072, 0x00070005,
    0x00000057, 0x545F6C67, 0x4C737365, 0x6C657665, 0x656E6E49, 0x00000072,
    0x00050048, 0x00000010, 0x00000000, 0x00000023, 0x00000008, 0x00050048,
    0x00000010, 0x00000001, 0x00000023, 0x0000000C, 0x00050048, 0x00000010,
    0x00000002, 0x00000023, 0x00000180, 0x00050048, 0x00000010, 0x00000003,
    0x00000023, 0x00000188, 0x00050048, 0x00000010, 0x00000004, 0x00000023,
    0x0000018C, 0x00030047, 0x00000010, 0x00000002, 0x00040047, 0x00000012,
    0x00000022, 0x00000001, 0x00040047, 0x00000012, 0x00000021, 0x00000000,
    0x00040047, 0x00000029, 0x0000001E, 0x00000000, 0x00040047, 0x0000002B,
    0x0000000B, 0x00000008, 0x00040047, 0x0000002D, 0x0000000B, 0x00000007,
    0x00040047, 0x00000039, 0x0000001E, 0x00000000, 0x00030047, 0x0000004B,
    0x0000000F, 0x00040047, 0x0000004B, 0x0000000B, 0x0000000B, 0x00030047,
    0x00000057, 0x0000000F, 0x00040047, 0x00000057, 0x0000000B, 0x0000000C,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015,
    0x00000006, 0x00000020, 0x00000000, 0x00030016, 0x00000008, 0x00000020,
    0x00040015, 0x0000000E, 0x00000020, 0x00000001, 0x00040017, 0x0000000F,
    0x00000008, 0x00000002, 0x0007001E, 0x00000010, 0x00000006, 0x0000000E,
    0x0000000F, 0x00000006, 0x00000006, 0x00040020, 0x00000011, 0x00000002,
    0x00000010, 0x0004003B, 0x00000011, 0x00000012, 0x00000002, 0x0004002B,
    0x0000000E, 0x00000013, 0x00000001, 0x00040020, 0x00000014, 0x00000002,
    0x0000000E, 0x0004002B, 0x00000006, 0x00000019, 0x00FFFFFF, 0x0004002B,
    0x0000000E, 0x0000001B, 0x00000003, 0x00040020, 0x0000001C, 0x00000002,
    0x00000006, 0x0004002B, 0x0000000E, 0x0000001F, 0x00000004, 0x0004002B,
    0x00000006, 0x00000026, 0x00000001, 0x0004001C, 0x00000027, 0x00000008,
    0x00000026, 0x00040020, 0x00000028, 0x00000003, 0x00000027, 0x0004003B,
    0x00000028, 0x00000029, 0x00000003, 0x00040020, 0x0000002A, 0x00000001,
    0x0000000E, 0x0004003B, 0x0000002A, 0x0000002B, 0x00000001, 0x0004003B,
    0x0000002A, 0x0000002D, 0x00000001, 0x00040020, 0x00000032, 0x00000003,
    0x00000008, 0x0004002B, 0x00000006, 0x00000036, 0x00000020, 0x0004001C,
    0x00000037, 0x00000008, 0x00000036, 0x00040020, 0x00000038, 0x00000001,
    0x00000037, 0x0004003B, 0x00000038, 0x00000039, 0x00000001, 0x00040020,
    0x0000003A, 0x00000001, 0x00000008, 0x0004002B, 0x0000000E, 0x0000003E,
    0x00000000, 0x0004002B, 0x0000000E, 0x00000045, 0x00000002, 0x0004002B,
    0x00000006, 0x00000048, 0x00000004, 0x0004001C, 0x00000049, 0x00000008,
    0x00000048, 0x00040020, 0x0000004A, 0x00000003, 0x00000049, 0x0004003B,
    0x0000004A, 0x0000004B, 0x00000003, 0x0004002B, 0x00000006, 0x00000054,
    0x00000002, 0x0004001C, 0x00000055, 0x00000008, 0x00000054, 0x00040020,
    0x00000056, 0x00000003, 0x00000055, 0x0004003B, 0x00000056, 0x00000057,
    0x00000003, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200F8, 0x00000005, 0x0004003D, 0x0000000E, 0x0000002C, 0x0000002B,
    0x0004003D, 0x0000000E, 0x0000002E, 0x0000002D, 0x0004007C, 0x00000006,
    0x0000002F, 0x0000002E, 0x00050041, 0x00000014, 0x00000063, 0x00000012,
    0x00000013, 0x0004003D, 0x0000000E, 0x00000064, 0x00000063, 0x0004007C,
    0x00000006, 0x00000065, 0x00000064, 0x00050080, 0x00000006, 0x00000066,
    0x0000002F, 0x00000065, 0x000500C7, 0x00000006, 0x00000067, 0x00000066,
    0x00000019, 0x00050041, 0x0000001C, 0x00000068, 0x00000012, 0x0000001B,
    0x0004003D, 0x00000006, 0x00000069, 0x00000068, 0x00050041, 0x0000001C,
    0x0000006A, 0x00000012, 0x0000001F, 0x0004003D, 0x00000006, 0x0000006B,
    0x0000006A, 0x0008000C, 0x00000006, 0x0000006C, 0x00000001, 0x0000002C,
    0x00000067, 0x00000069, 0x0000006B, 0x00040070, 0x00000008, 0x0000006D,
    0x0000006C, 0x00050041, 0x00000032, 0x00000033, 0x00000029, 0x0000002C,
    0x0003003E, 0x00000033, 0x0000006D, 0x00050041, 0x0000003A, 0x0000003B,
    0x00000039, 0x0000001B, 0x0004003D, 0x00000008, 0x0000003C, 0x0000003B,
    0x00050041, 0x0000003A, 0x0000003F, 0x00000039, 0x0000003E, 0x0004003D,
    0x00000008, 0x00000040, 0x0000003F, 0x00050041, 0x0000003A, 0x00000042,
    0x00000039, 0x00000013, 0x0004003D, 0x00000008, 0x00000043, 0x00000042,
    0x00050041, 0x0000003A, 0x00000046, 0x00000039, 0x00000045, 0x0004003D,
    0x00000008, 0x00000047, 0x00000046, 0x00050041, 0x00000032, 0x0000004D,
    0x0000004B, 0x0000003E, 0x0003003E, 0x0000004D, 0x0000003C, 0x00050041,
    0x00000032, 0x0000004F, 0x0000004B, 0x00000013, 0x0003003E, 0x0000004F,
    0x00000040, 0x00050041, 0x00000032, 0x00000051, 0x0000004B, 0x00000045,
    0x0003003E, 0x00000051, 0x00000043, 0x00050041, 0x00000032, 0x00000053,
    0x0000004B, 0x0000001B, 0x0003003E, 0x00000053, 0x00000047, 0x0007000C,
    0x00000008, 0x0000005A, 0x00000001, 0x00000025, 0x00000040, 0x00000047,
    0x00050041, 0x00000032, 0x0000005B, 0x00000057, 0x0000003E, 0x0003003E,
    0x0000005B, 0x0000005A, 0x0007000C, 0x00000008, 0x0000005E, 0x00000001,
    0x00000025, 0x0000003C, 0x00000043, 0x00050041, 0x00000032, 0x0000005F,
    0x00000057, 0x00000013, 0x0003003E, 0x0000005F, 0x0000005E, 0x000100FD,
    0x00010038,
};
