using System.IO;

namespace UnrealBuildTool.Rules
{
	public class Simulation : ModuleRules
	{
		public Simulation(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
					"Simulation/Private",
                    "ShaderUtility/Public"
                }
                );

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
                      "Core", "CoreUObject", "Engine", "RenderCore", "Landscape", "RHI",
                        "SimplexNoise", "ShaderUtility", "SimulationData", "SimulationPixelShader"
                }
				);

		}
	}
}