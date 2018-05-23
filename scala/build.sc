import mill._
import mill.scalalib._
import mill.scalalib.publish._

object core extends ScalaModule with PublishModule {
  //==================
  // Publication Stuff
  //==================
  override def artifactName = T{ "ticklish-scala" }
  def publishVersion = "2.0.0-SNAPSHOT"
  def pomSettings = PomSettings(
    description = "Ticklish interface library in Scala",
    organization = "com.github.ichoran",
    url = "https://github.com/ichoran/ticklish",
    licenses = Seq(License.`Apache-2.0`),
    versionControl = VersionControl.github("ichoran", "ticklish"),
    developers = Seq(
      Developer("ichoran", "Rex Kerr","https://github.com/ichoran")
    )
  )


  //=================
  // Core Build Stuff
  //=================
  def scalaVersion = "2.12.4"

  def repositories() = super.repositories ++ Seq(
    coursier.maven.MavenRepository("https://oss.sonatype.org/content/repositories/snapshots")
  )

  def ivyDeps = Agg(
    //ivy"com.github.ichoran::kse:0.7-SNAPSHOT",
    ivy"org.scream3r:jssc:2.8.0"
  )

  def scalacOptions = T{ Seq(
    "-unchecked",
    "-feature",
    "-deprecation",
    "-opt:l:method"
  )}
}
