#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <sys/time.h>
#define RCVSIZE 1024
#define pack_size 1472

long size_fichier(FILE *f){
	fseek(f,0,SEEK_END);
  return ftell(f);
}

long number_of_frames(int size){
	if (size%pack_size==0) return size/pack_size;
	else return (size/pack_size)+1;
}
	
// rtt
// measure rtt (with weighed moving average).
long measure_rtt(long start, long cur, long rtt)
{ 
  long cur_rtt =cur-start;
  long new_rtt;
  if(rtt == 0)
  {
    // first measurement
    new_rtt = cur_rtt;
  }
  else
  {
    // weighed moving average
    new_rtt = 0.8*rtt + 0.2*cur_rtt; //alpha=0.8
  }
  return new_rtt;
}

int main(int argc, char *argv[])
{
	int sockfd_conx,sockfd_msg; 
	struct sockaddr_in servaddr_conx, servaddr_msg,cliaddr1,cliaddr2;
	char buffer_conx[RCVSIZE]; 
	char syn[]="SYN"; 
  char syn_ack[]="SYN-ACK9000";/////attention cas multi-clients
  char ack[]="ACK";
	char fin[]="FIN";
	char nom_fichier[100];
	FILE *file;
	int reuse = 1;
	// Tester les arguments d'entrée
	if ((argc < 2) || (argc > 2)) {				
		printf("Essayer avec : [%s] [Port Number]\n", argv[0]);		
		exit(EXIT_FAILURE);
	}

	// Création de socket connexion
	if ( (sockfd_conx = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	}
	memset(&servaddr_conx, 0, sizeof(servaddr_conx));
	// Remplir les champs de la structure socket
	servaddr_conx.sin_family = AF_INET; 
	servaddr_conx.sin_addr.s_addr = INADDR_ANY; 
	servaddr_conx.sin_port = htons(atoi(argv[1]));
	setsockopt(sockfd_conx, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	// Binding
	if ( bind(sockfd_conx, (const struct sockaddr *)&servaddr_conx, 
	  sizeof(servaddr_conx)) < 0 ) 
	{ 
		perror("bind failed"); 
		exit(EXIT_FAILURE); 
	}
	
	int len = sizeof(cliaddr1);
	// Réception de SYN
	recvfrom(sockfd_conx, (char *)buffer_conx, RCVSIZE,MSG_WAITALL, ( struct sockaddr *) &cliaddr1,&len); 
	// Envoie de SYN-ACK avec le numéro de port résevé aux messages
	if (strcmp(buffer_conx,syn)==0){
		printf("SYN received\n"); 
  	sendto(sockfd_conx, (const char *)syn_ack, strlen(syn_ack),MSG_CONFIRM, (const struct sockaddr *) &cliaddr1,len);
		printf("SYN-ACK sent from server\n"); 
	}
	else return(0); // avec while continu
	
	// Créatin de la socket pour l’échange de données

	if ((sockfd_msg = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	}
	memset(&servaddr_msg, 0, sizeof(servaddr_msg));
	// Remplir les champs de la structure socket
	servaddr_msg.sin_family = AF_INET; 
	servaddr_msg.sin_addr.s_addr = INADDR_ANY; 
	servaddr_msg.sin_port = htons(9000);
	setsockopt(sockfd_msg, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	// Binding
	if ( bind(sockfd_msg, (const struct sockaddr *)&servaddr_msg, 
		sizeof(servaddr_msg)) < 0 ) 
	{ 
		perror("bind failed"); 
		exit(EXIT_FAILURE); 
	}

	// Création d'un child process qui se chargera de l'échange des données
	int fils = fork();
	if (fils == -1) {              
    perror("erreur de création du child process");           
    exit(EXIT_FAILURE);       
    }
	else if (fils >0){
		memset(&buffer_conx, 0, sizeof(buffer_conx));
		recvfrom(sockfd_conx, (char *)buffer_conx, RCVSIZE,MSG_WAITALL, ( struct sockaddr *) &cliaddr1,&len);
		
		if (strcmp(buffer_conx,ack)==0) {
	  		printf("ACK reçu ==> connexion établie avec succès\n"); 
	  	}
		else {
			kill(fils, SIGKILL);
			printf("ACK non reçu ==> établissement échoué\n");
		}
		close(sockfd_msg);
	}
	else {
		close(sockfd_conx);
		int len2 = sizeof(cliaddr1);
		recvfrom(sockfd_msg, (char *)nom_fichier, 100,MSG_WAITALL, ( struct sockaddr *) &cliaddr2,&len2);
		file=fopen(nom_fichier,"rb");
    if (file == NULL) 
    { 
      printf("Ouverture du fichier impossible"); 
      exit(0);
    }
		else{ 
			memset(nom_fichier, 0, sizeof(nom_fichier)); 
		}
		
		// taille du fichier	
		int taille=size_fichier(file); 
		fseek(file, 0L, SEEK_SET);
		// Nombre de packets nécessaires
		int total_seg=number_of_frames(taille);
		float ssthresh=80, flight_size; float window=1;
		int num_seq_courant=0;
		int dernier_num_seg_aquitte=0,dernier_num_seg_aquitte_prec=0;
		char segment_utile[pack_size];
		char *segment;
		char aquittement[10];
		fd_set rdset, wrset;
		int numero_aquitement;
		int i=1;
		char c[7];
		char d[7];
		struct timeval tv;
		tv.tv_sec = 0;
    tv.tv_usec = 0;
		int borne_sup_w=0;
		int flag=1;
		struct timeval start, end,tx_time, time_now,now_boucle;
		int m=0;
		long int temp_transfert;
		long timeout; //timeout
		int flag2=0, flag_ack=0;
		long rtt = 0; // in micro s
		int nbre_tout=0;
		long instants_tx[total_seg+1]; 
		char flags_rtx[total_seg+1];
		memset(flags_rtx, 0, sizeof(flags_rtx));
		memset(instants_tx, 0, sizeof(instants_tx));
		int r,x=1, nbre_rtx=0,nbre_rtx_wile=0, comp=0;
		long position_file;int b=0, duplique=0;
		while(dernier_num_seg_aquitte!=total_seg){		

			if (flag==1) {timeout=50000;}
			else {timeout=2*rtt; }//micro s

			if (flag_ack==1) {
				flag_ack=0;
				recvfrom(sockfd_msg, (char *)aquittement, sizeof(aquittement),MSG_WAITALL, ( struct sockaddr *)&cliaddr2,&len2);
				memcpy(c,aquittement+sizeof(ack)-1, sizeof(c));
				numero_aquitement=atoi(c);
					
				if (numero_aquitement>dernier_num_seg_aquitte) {
					if (window > ssthresh){ window+=(numero_aquitement-dernier_num_seg_aquitte)/window;}
					else {window+=numero_aquitement-dernier_num_seg_aquitte;}
					dernier_num_seg_aquitte=numero_aquitement;
					x=0;flag2=1;
				}
				gettimeofday(&time_now, NULL);
				long now=time_now.tv_sec * 1000000 + time_now.tv_usec;

				if (flags_rtx[numero_aquitement]!=1 && dernier_num_seg_aquitte>=borne_sup_w){ //RTT update
					flag=0;
					rtt=measure_rtt(instants_tx[numero_aquitement],now,rtt);
				}
				
				if (numero_aquitement==dernier_num_seg_aquitte) { //Fast retransmit
					if (x==0) {comp=0;x=1;}
					comp++;
					if (comp==4) {
						flight_size=window;
						ssthresh= flight_size/2;
						window= ssthresh+ 40;
						duplique++;
						gettimeofday(&now_boucle, NULL);
						long now=now_boucle.tv_sec * 1000000 + now_boucle.tv_usec;
						memset(segment_utile, 0, sizeof(segment_utile));
						fseek(file,(dernier_num_seg_aquitte)*pack_size ,SEEK_SET);
						r=fread(segment_utile, 1, pack_size, file);
						segment= (char *)calloc(r+6, sizeof(char));
						memset(segment, 0, sizeof(segment));
						memset(d, 0, sizeof(d));
						sprintf(d,"%06d",dernier_num_seg_aquitte+1);
						memcpy(segment,d,6);
            memcpy(segment+6,segment_utile,r);
						sendto(sockfd_msg, (const char *)segment, r+6,MSG_CONFIRM, (const struct sockaddr *)&cliaddr2,len2);
						instants_tx[dernier_num_seg_aquitte+1]=now;
						fseek(file,position_file ,SEEK_SET);
						comp=0;
					}	
				}	
			}

			if (flag2==1 ){ //ACK utile
				borne_sup_w=dernier_num_seg_aquitte+1;flag2=0;//dernier_num_seg_aquitte_prec=dernier_num_seg_aquitte;
				if (num_seq_courant<=dernier_num_seg_aquitte) {
				num_seq_courant=dernier_num_seg_aquitte+1;
				fseek(file,dernier_num_seg_aquitte*pack_size ,SEEK_SET);
				}	
			}
			else if (m!=0) { //Retransmission de la borne sup
				gettimeofday(&now_boucle, NULL);
				long now=now_boucle.tv_sec * 1000000 + now_boucle.tv_usec;
				for (int k=borne_sup_w;k<=(num_seq_courant-1);k++){ //Retransmission
					FD_ZERO(&rdset); //???
					FD_SET(sockfd_msg, &rdset);
					if (select(sockfd_msg+1, &rdset, NULL, NULL, &tv)>0) {flag_ack=1;break;}
					
					if (now>=instants_tx[k]+timeout) {
						memset(segment_utile, 0, sizeof(segment_utile));
						fseek(file,(k-1)*pack_size ,SEEK_SET);
						r = fread(segment_utile, 1, pack_size, file);
						segment= (char *)calloc(r+6, sizeof(char));
						memset(segment, 0, sizeof(segment));
						memset(d, 0, sizeof(d));
						sprintf(d,"%06d", k);
						memcpy(segment,d,6);
            memcpy(segment+6,segment_utile,r);
						sendto(sockfd_msg, (const char *)segment, r+6,MSG_CONFIRM, (const struct sockaddr *)&cliaddr2,len2);
						flags_rtx[k]=1; //le paquet retransmis est non utilisé pour RTT update
						
						nbre_rtx++;
						gettimeofday(&now_boucle, NULL);
						long now1=now_boucle.tv_sec * 1000000 + now_boucle.tv_usec;
						instants_tx[k]=now1;
						fseek(file,position_file ,SEEK_SET);
					}
				}
			}
			if (m==0) {num_seq_courant=dernier_num_seg_aquitte+1;borne_sup_w=num_seq_courant;
				fseek(file,(borne_sup_w-1)*pack_size ,SEEK_SET);}
			//printf("window===>%f\n",window);
			while ((num_seq_courant<borne_sup_w+window && num_seq_courant<=total_seg)) {
				FD_ZERO(&rdset);
				FD_SET(sockfd_msg, &rdset);
				if(select(sockfd_msg+1, &rdset, NULL, NULL, &tv)>0) {flag_ack=1;break;}
				memset(segment_utile, 0, sizeof(segment_utile));
				r = fread(segment_utile, 1, pack_size, file);
				segment= (char *)calloc(r+6, sizeof(char));
				memset(segment, 0, sizeof(segment));
				memset(d, 0, sizeof(d));
				sprintf(d,"%06d", num_seq_courant);
				memcpy(segment,d,6);
        memcpy(segment+6,segment_utile,r);
				
				sendto(sockfd_msg, (const char *)segment, r+6,MSG_CONFIRM, (const struct sockaddr *) &cliaddr2,len2);   
				
   			gettimeofday(&tx_time, NULL);
				instants_tx[num_seq_courant]=tx_time.tv_sec * 1000000 + tx_time.tv_usec;
				position_file=ftell(file);
				gettimeofday(&now_boucle, NULL);
				long now=now_boucle.tv_sec * 1000000 + now_boucle.tv_usec;
				for (int k=borne_sup_w;k<=num_seq_courant;k++) { //Retransmission
					if (now>=instants_tx[k]+timeout) {
						//if(select(sockfd_msg+1, &rdset, NULL, NULL, &tv)>0) {break;}
						FD_ZERO(&rdset);//???
						FD_SET(sockfd_msg, &rdset);
						if (select(sockfd_msg+1, &rdset, NULL, NULL, &tv)>0) {b=1;flag_ack=1; break;}
						memset(segment_utile, 0, sizeof(segment_utile));
						fseek(file,(k-1)*pack_size ,SEEK_SET);
						r=fread(segment_utile, 1, pack_size, file);
						segment= (char *)calloc(r+6, sizeof(char));
						memset(segment, 0, sizeof(segment));
						memset(d, 0, sizeof(d));
						sprintf(d,"%06d", k);
						memcpy(segment,d,6);
            memcpy(segment+6,segment_utile,r);
						sendto(sockfd_msg, (const char *)segment, r+6,MSG_CONFIRM, (const struct sockaddr *)&cliaddr2,len2);
						
						flags_rtx[k]=1; //le paquet retransmis est non utilisé pour RTT update
						nbre_rtx_wile++;
						gettimeofday(&now_boucle, NULL);
						long now1=now_boucle.tv_sec * 1000000 + now_boucle.tv_usec;
						instants_tx[k]=now1;
						fseek(file,position_file ,SEEK_SET);
					}
				}	
				gettimeofday(&now_boucle, NULL);
				if(m==0){gettimeofday(&start, NULL);m=1;}
				num_seq_courant+=1;
				if(b==1){b=0;break;}				
			}
			i=i+1;
		}
		sendto(sockfd_msg, (const char *)fin, strlen(fin),MSG_CONFIRM, (const struct sockaddr *) &cliaddr1,len);
		fclose(file);
		close(sockfd_msg);
    exit(0);				
	}	
						
	return(0);
		
}











