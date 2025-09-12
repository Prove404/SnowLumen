using System.IO;

namespace UnrealBuildTool.Rules
{
	public class SimulationPixelShader : ModuleRules
	{
		public SimulationPixelShader(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
					"SimulationPixelShader/Private",
                    "ShaderUtility/Public"
				}
                );

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "RenderCore",
                    "RHI",
                    "ShaderUtility",
                    "Projects"
				}
				);

		}
	}
}