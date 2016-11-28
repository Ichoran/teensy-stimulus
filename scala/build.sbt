lazy val root = (project in file(".")).
  settings(
    scalaVersion := "2.12.0",
    name := "teensy-stim-interface",
    version := "0.1.0",
    scalacOptions ++= Seq("-unchecked", "-feature", "-deprecation"),
    libraryDependencies += "org.scream3r" % "jssc" % "2.8.0",
    mainClass in Compile := Some("lab.kerrr.teensystim")
  )
