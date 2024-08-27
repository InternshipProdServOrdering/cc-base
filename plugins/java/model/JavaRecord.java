package model;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Table;

@Entity
@Table(name = "\"JavaRecord\"")
public class JavaRecord extends JavaEntity {
  @Column(name = "\"isAbstract\"")
  private boolean isAbstract;

  @Column(name = "\"isFinal\"")
  private boolean isFinal;

  @Column(name = "\"isStatic\"")
  private boolean isStatic;

  // Getters and setters

  public boolean isAbstract() {
    return isAbstract;
  }

  public void setAbstract(boolean anAbstract) {
    isAbstract = anAbstract;
  }

  public boolean isFinal() {
    return isFinal;
  }

  public void setFinal(boolean aFinal) {
    isFinal = aFinal;
  }

  public boolean isStatic() {
    return isStatic;
  }

  public void setStatic(boolean aStatic) {
    isStatic = aStatic;
  }
}
