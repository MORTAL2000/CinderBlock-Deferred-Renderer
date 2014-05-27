#version 120

//varying vec4 sPos;
//
//void main(void)
//{
//	gl_Position = ftransform();
//    sPos = gl_Position;
//}

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;

void main()
{
    vec4 position = gl_Vertex;                  //in
    
    vec4 mvPosition = modelViewMatrix * vec4( position.xyz, 1.0 );
    gl_Position = projectionMatrix * mvPosition;
}