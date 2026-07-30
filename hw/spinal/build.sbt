val spinalVersion = "1.12.0"

lazy val root = (project in file("."))
  .settings(
    name := "OpenGPUSpinal",
    inThisBuild(List(
      organization := "org.opengpu",
      scalaVersion := "2.13.12",
      version := "0.1.0"
    )),
    libraryDependencies ++= Seq(
      "com.github.spinalhdl" %% "spinalhdl-core" % spinalVersion,
      "com.github.spinalhdl" %% "spinalhdl-lib" % spinalVersion,
      compilerPlugin("com.github.spinalhdl" %% "spinalhdl-idsl-plugin" % spinalVersion),
      "org.yaml" % "snakeyaml" % "1.8",
      "org.scalatest" %% "scalatest" % "3.2.19" % Test,
    ),
    target := baseDirectory.value / "build" / "sbt" / "target",
    Compile / run / fork := true,
    Compile / run / forkOptions := (Compile / run / forkOptions).value
      .withWorkingDirectory(baseDirectory.value / "build")
  )
